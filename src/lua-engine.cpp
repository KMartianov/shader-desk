// src/lua-engine.cpp
#include "lua-engine.hpp"
#include <iostream>
#include <filesystem>
#include <map>
#include "data-provider.hpp"

namespace fs = std::filesystem;

static std::string sanitize_plugin_name(std::string name) {
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
        if (std::isspace(c) || c == '-') return '_';
        return (char)std::tolower(c);
    });
    return name;
}

std::string LuaEngine::get_config_dir() {
    if (!config_dir.empty()) return config_dir;

    const char* xdg_config = std::getenv("XDG_CONFIG_HOME");
    if (xdg_config && *xdg_config) {
        config_dir = std::string(xdg_config) + "/interactive-wallpaper";
    } else {
        config_dir = std::string(std::getenv("HOME")) + "/.config/interactive-wallpaper";
    }
    return config_dir;
}

bool LuaEngine::load() {
    std::string dir = get_config_dir();
    fs::path plugins_dir = fs::path(dir) / "plugins";
    fs::path init_lua_path = fs::path(dir) / "init.lua";

    // 1. Полностью очищаем состояние (полезно при hot-reload)
    lua = sol::state();
    
    // 2. Открываем стандартные безопасные библиотеки Lua
    lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::os, sol::lib::string, sol::lib::table, sol::lib::package);

    // Добавляем директорию конфига в package.path
    std::string package_path = lua["package"]["path"];
    lua["package"]["path"] = package_path + ";" + dir + "/?.lua";

    // 3. Создаем глобальные таблицы, чтобы скрипты не падали
    lua["config"] = lua.create_table();
    
    sol::table core = lua.create_table();
    lua["core"] = core;
    
    sol::table utils = lua.create_table();
    sol::table debug = lua.create_table();
    core["utils"] = utils;
    core["debug"] = debug;

    //Заново привязываем C++ API к свежесозданному sol::state!

    if (current_core) {
        bind_core_api(current_core);
    }


    // --- РЕГИСТРАЦИЯ СИСТЕМЫ ПРЕСЕТОВ ---
    utils["apply_preset"] = [this](sol::table target, const std::string& plugin_name, const std::string& preset_name) {
        // Функция sanitize_plugin_name должна быть объявлена выше в этом же файле
        std::string preset_path = this->get_config_dir() + "/presets/" + sanitize_plugin_name(plugin_name) + "/" + preset_name + ".lua";
        
        if (!fs::exists(preset_path)) {
            std::cout << "\033[33m[Warning] Preset not found: " << preset_path << "\033[0m\n";
            return;
        }
        
        try {
            // Выполняем файл пресета (он должен вернуть Lua-таблицу)
            sol::protected_function_result result = lua.script_file(preset_path);
            if (result.valid() && result.get_type() == sol::type::table) {
                sol::table preset_data = result;
                // Shallow merge: копируем ключи из пресета в целевую таблицу эффекта
                for (auto& kv : preset_data) {
                    target[kv.first] = kv.second;
                }
                std::cout << "  -> Applied preset '" << preset_name << "' to '" << plugin_name << "'\n";
            } else {
                std::cerr << "[Warning] Preset file '" << preset_name << "' did not return a valid table.\n";
            }
        } catch (const std::exception& e) {
            std::cerr << "[Error] Failed to load preset: " << e.what() << "\n";
        }
    };

    try {
        // 4. Сначала исполняем все автосгенерированные конфиги плагинов (conf.d)
        if (fs::exists(plugins_dir) && fs::is_directory(plugins_dir)) {
            for (const auto& entry : fs::directory_iterator(plugins_dir)) {
                if (entry.path().extension() == ".lua") {
                    lua.script_file(entry.path().string());
                }
            }
        }

        // 5. Затем исполняем главный init.lua (Логика пользователя)
        if (fs::exists(init_lua_path)) {
            lua.script_file(init_lua_path.string());
        } else {
            std::cerr << "Warning: init.lua not found. Run with --init-config to generate." << std::endl;
            return false;
        }

        std::cout << "Lua configuration loaded successfully." << std::endl;
        return true;

    } catch (const sol::error& e) {
        std::cerr << "CRITICAL LUA ERROR: " << e.what() << std::endl;
        std::cerr << "Falling back to previous state or defaults." << std::endl;
        return false;
    }
}

// --- РЕАЛИЗАЦИЯ КОНФИГУРАЦИИ ПРОВАЙДЕРОВ ---
bool LuaEngine::configure_provider(IDataProvider* provider) {
    if (!provider) return false;

    // БЕЗОПАСНОЕ чтение: проверяем, что объект существует и является таблицей
    sol::object core_obj = lua["core"];
    if (!core_obj.is<sol::table>()) return true; // Если нет, провайдер включен по умолчанию
    sol::table core = core_obj.as<sol::table>();

    sol::object providers_obj = core["providers"];
    if (!providers_obj.is<sol::table>()) return true; 
    sol::table providers = providers_obj.as<sol::table>();

    sol::object p_conf_obj = providers[provider->get_name()];
    if (!p_conf_obj.is<sol::table>()) return true;
    sol::table p_conf = p_conf_obj.as<sol::table>();

    // Проверяем флаг включения
    bool enabled = p_conf.get_or("enabled", true);
    if (!enabled) return false;

    // Извлекаем ожидаемые типы параметров провайдера
    auto params = provider->get_parameters();
    std::map<std::string, size_t> expected_types;
    for (const auto& p : params) expected_types[p.name] = p.value.index();

    // Применяем значения из Lua
    for (const auto& kv : p_conf) {
        if (!kv.first.is<std::string>()) continue;
        std::string key = kv.first.as<std::string>();
        if (key == "enabled") continue;

        sol::object val = kv.second;
        auto it = expected_types.find(key);
        if (it == expected_types.end()) continue;

        size_t expected_type = it->second;
        try {
            if (expected_type == 0 && val.is<bool>()) {
                provider->set_parameter(key, val.as<bool>());
            } else if (expected_type == 1 && val.is<double>()) {
                provider->set_parameter(key, val.as<int>());
            } else if (expected_type == 2 && val.is<double>()) {
                provider->set_parameter(key, static_cast<float>(val.as<double>()));
            }
        } catch (const sol::error& e) {
            std::cerr << "Lua Type Error for provider parameter '" << key << "': " << e.what() << std::endl;
        }
    }

    return true;
}

// --- РЕАЛИЗАЦИЯ DEBUG API ---
void LuaEngine::bind_core_api(ICoreContext* core) {
    if (!core) return;
    current_core = core; // Сохраняем для использования в clear_timers()
    
     if (lua["core"].get_type() == sol::type::none) {
        lua["core"] = lua.create_table();
    }
    sol::table core_table = lua["core"];
    
    // 1. API записи в BlackBoard из Lua (остается как обсуждали)
    core_table["set_string"] = [core](const std::string& key, const std::string& val) {
        core->get_blackboard().set_string(key, val);
    };

    // 2. Создание таймера. Возвращает ID (файл-дескриптор)
    core_table["set_interval"] = [this](int ms, sol::protected_function callback) -> int {
        if (ms <= 0 || !callback.valid() || !current_core) return -1;

        // Создаем неблокирующий таймер
        int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
        if (tfd == -1) {
            std::cerr << "[LuaEngine] Failed to create timerfd: " << strerror(errno) << std::endl;
            return -1;
        }

        struct itimerspec ts{};
        ts.it_interval.tv_sec = ms / 1000;
        ts.it_interval.tv_nsec = (ms % 1000) * 1000000;
        ts.it_value = ts.it_interval;

        if (timerfd_settime(tfd, 0, &ts, nullptr) == -1) {
            std::cerr << "[LuaEngine] Failed to set timerfd: " << strerror(errno) << std::endl;
            close(tfd);
            return -1;
        }

        active_timers.insert(tfd);

        // Регистрируем в epoll Ядра
        current_core->register_epoll_fd(tfd, [this, tfd, callback](uint32_t events) {
            // [ЗАЩИТА]: Если таймер был отменен (или идет hot-reload), игнорируем
            if (active_timers.find(tfd) == active_timers.end()) return;

            uint64_t expirations;
            ssize_t s = read(tfd, &expirations, sizeof(expirations));
            
            if (s == sizeof(expirations)) {
                auto result = callback();
                if (!result.valid()) {
                    sol::error err = result;
                    std::cerr << "[Lua Timer Error] " << err.what() << std::endl;
                }
            } else if (s == -1 && errno != EAGAIN) {
                std::cerr << "[LuaEngine] Error reading timerfd: " << strerror(errno) << std::endl;
            }
        });

        return tfd; // Отдаем Lua дескриптор в качестве ID таймера
    };

    // 3. Отмена таймера (по ID)
    core_table["clear_interval"] = [this](int tfd) {
        if (tfd < 0) return;
        
        if (active_timers.erase(tfd)) {
            if (current_core) {
                current_core->unregister_epoll_fd(tfd); // Убираем из epoll
            }
            close(tfd); // Закрываем дескриптор ядра Linux
        }
    };
}

void LuaEngine::clear_timers() {
    for (int tfd : active_timers) {
        if (current_core) {
            current_core->unregister_epoll_fd(tfd);
        }
        close(tfd);
    }
    active_timers.clear();
}

bool LuaEngine::reload() {
    std::cout << "[LuaEngine] Clearing active timers before reload..." << std::endl;
    clear_timers(); // Обязательно вычищаем системные таймеры и epoll перед убийством sol::state
    return load();
}

std::string LuaEngine::get_active_effect() const {
    sol::table core = lua["core"];
    if (core.valid()) {
        return core.get_or("active_effect", std::string(""));
    }
    return "";
}

bool LuaEngine::is_interactive() const {
    sol::table core = lua["core"];
    if (core.valid()) {
        return core.get_or("interactive", true);
    }
    return true;
}

void LuaEngine::apply_effect_settings(WallpaperEffect* effect, const std::string& effect_name) {
    if (!effect) return;

    sol::table config = lua["config"];
    if (!config.valid()) return;

    sol::table effect_settings = config[effect_name];
    if (!effect_settings.valid()) return;

    // Получаем типы параметров, которые ОЖИДАЕТ плагин.
    // Это защита от падения (std::bad_variant_access), так как в Lua числа не типизированы жестко.
    auto default_params = effect->get_parameters();
    std::map<std::string, size_t> expected_types;
    for (const auto& p : default_params) {
        expected_types[p.name] = p.value.index(); 
        // 0: bool, 1: int, 2: float, 3: glm::vec3, 4: std::string
    }

    // Итерируемся по Lua-таблице плагина
    for (const auto& key_value_pair : effect_settings) {
        if (!key_value_pair.first.is<std::string>()) continue;
        
        std::string key = key_value_pair.first.as<std::string>();
        sol::object val = key_value_pair.second;

        auto it = expected_types.find(key);
        if (it == expected_types.end()) {
            // Плагин не знает такой параметр (возможно, устарел)
            continue;
        }

        size_t expected_type = it->second;

        try {
            if (expected_type == 0 && val.is<bool>()) {
                effect->set_parameter(key, val.as<bool>());
            } 
            else if (expected_type == 1 && val.is<double>()) { // В Lua все числа double
                effect->set_parameter(key, val.as<int>());
            } 
            else if (expected_type == 2 && val.is<double>()) {
                effect->set_parameter(key, static_cast<float>(val.as<double>()));
            } 
            else if (expected_type == 3 && val.is<sol::table>()) {
                sol::table t = val.as<sol::table>();
                // Проверяем, что это массив из 3 элементов
                if (t.size() >= 3) {
                    float r = t.get_or(1, 0.0f);
                    float g = t.get_or(2, 0.0f);
                    float b = t.get_or(3, 0.0f);
                    effect->set_parameter(key, glm::vec3(r, g, b));
                }
            }
            else if (expected_type == 4 && val.is<std::string>()) {
                effect->set_parameter(key, val.as<std::string>());
            }
        } catch (const sol::error& e) {
            std::cerr << "Lua Type Error for parameter '" << key << "': " << e.what() << std::endl;
        }
    }
}