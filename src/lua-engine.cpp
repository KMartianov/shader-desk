// src/lua-engine.cpp
#include "lua-engine.hpp"
#include <iostream>
#include <filesystem>
#include <map>
#include "data-provider.hpp" // Теперь подключает SDK-обертку (IProviderABI)

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

    // Заново привязываем C++ API к свежесозданному sol::state!
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
// [ABI UPDATE]: Теперь мы принимаем безопасный IDataProviderABI*
bool LuaEngine::configure_provider(IDataProviderABI* provider) {
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

    // [ABI UPDATE]: Извлекаем ожидаемые типы параметров провайдера через C-API
    std::map<std::string, ParamType> expected_types;
    uint32_t count = provider->get_parameter_count();
    for (uint32_t i = 0; i < count; ++i) {
        ParamInfoABI info;
        provider->get_parameter_info(i, &info);
        expected_types[info.name] = info.default_value.type;
    }

    // Применяем значения из Lua
    for (const auto& kv : p_conf) {
        if (!kv.first.is<std::string>()) continue;
        std::string key = kv.first.as<std::string>();
        if (key == "enabled") continue;

        sol::object val = kv.second;
        auto it = expected_types.find(key);
        if (it == expected_types.end()) continue;

        ParamType expected_type = it->second;
        ParamValueABI abi_val;
        abi_val.type = expected_type;

        try {
            // [ВНИМАНИЕ]: Эта строка должна жить до конца итерации. 
            // Это позволяет безопасно передать сырой указатель .c_str() через границу ABI.
            std::string temp_str;

            if (expected_type == ParamType::TYPE_BOOL && val.is<bool>()) {
                abi_val.b_val = val.as<bool>();
                provider->set_parameter(key.c_str(), &abi_val);
            } 
            else if (expected_type == ParamType::TYPE_INT && val.is<double>()) {
                abi_val.i_val = val.as<int>();
                provider->set_parameter(key.c_str(), &abi_val);
            } 
            else if (expected_type == ParamType::TYPE_FLOAT && val.is<double>()) {
                abi_val.f_val = static_cast<float>(val.as<double>());
                provider->set_parameter(key.c_str(), &abi_val);
            }
        } catch (const sol::error& e) {
            std::cerr << "Lua Type Error for provider parameter '" << key << "': " << e.what() << std::endl;
        }
    }

    return true;
}

// --- РЕАЛИЗАЦИЯ DEBUG API ---
// [ABI UPDATE]: Принимаем ICoreContextABI*
void LuaEngine::bind_core_api(ICoreContextABI* core) {
    if (!core) return;
    current_core = core; // Сохраняем для использования в clear_timers()
    
    if (lua["core"].get_type() == sol::type::none) {
        lua["core"] = lua.create_table();
    }
    sol::table core_table = lua["core"];
    
    // 1. API записи в BlackBoard из Lua (остается как обсуждали)
    core_table["set_string"] = [core](const std::string& key, const std::string& val) {
        // [ABI UPDATE]: Вызываем безопасные C-методы, передавая const char*
        core->get_blackboard()->set_string(key.c_str(), val.c_str());
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

        // [ABI FIX / LEAK FIX]: 
        // Мы сохраняем C++ лямбду в std::unordered_map (active_timers) внутри самого LuaEngine.
        // Это гарантирует, что память под лямбду не утечет, и она будет уничтожена при удалении таймера.
        active_timers[tfd] = [this, tfd, callback](uint32_t events) {
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
        };

        // Регистрируем в epoll Ядра через ABI-совместимый интерфейс.
        // Мы передаем указатель на сохраненную в мапе лямбду (&active_timers[tfd]) как user_data.
        // Стандарт C++ гарантирует, что указатели на элементы std::unordered_map не инвалидируются при вставке других элементов!
        current_core->register_epoll_fd(tfd, [](uint32_t events, void* user_data) {
            auto* cb = static_cast<std::function<void(uint32_t)>*>(user_data);
            if (cb && *cb) {
                (*cb)(events);
            }
        }, &active_timers[tfd]);

        return tfd; // Отдаем Lua дескриптор в качестве ID таймера
    };

    // 3. Отмена таймера (по ID)
    core_table["clear_interval"] = [this](int tfd) {
        if (tfd < 0) return;
        
        // [ABI FIX]: Метод erase() автоматически вызывает деструктор std::function, 
        // полностью очищая память.
        if (active_timers.erase(tfd)) {
            if (current_core) {
                current_core->unregister_epoll_fd(tfd); // Убираем из epoll
            }
            close(tfd); // Закрываем дескриптор ядра Linux
        }
    };
}

void LuaEngine::clear_timers() {
    // [ABI UPDATE]: active_timers теперь map, поэтому итерируемся по парам (key, value)
    for (auto& pair : active_timers) {
        int tfd = pair.first;
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

// [ABI UPDATE]: Теперь мы принимаем безопасный IWallpaperEffectABI*
void LuaEngine::apply_effect_settings(IWallpaperEffectABI* effect, const std::string& effect_name) {
    if (!effect) return;

    sol::table config = lua["config"];
    if (!config.valid()) return;

    sol::table effect_settings = config[effect_name];
    if (!effect_settings.valid()) return;

    // [ABI UPDATE]: Получаем типы параметров, которые ОЖИДАЕТ плагин через C-интерфейс.
    // Это защита от падения, так как в Lua числа не типизированы жестко.
    std::map<std::string, ParamType> expected_types;
    uint32_t count = effect->get_parameter_count();
    for (uint32_t i = 0; i < count; ++i) {
        ParamInfoABI info;
        effect->get_parameter_info(i, &info);
        expected_types[info.name] = info.default_value.type; 
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

        ParamType expected_type = it->second;
        ParamValueABI abi_val;
        abi_val.type = expected_type;

        try {
            // [ВНИМАНИЕ]: Временная строка для сохранения контекста памяти во время вызова C-функции.
            // Она будет уничтожена только после выхода из блока try, что гарантирует
            // валидность указателя abi_val.s_val внутри плагина во время вызова set_parameter.
            std::string temp_str;

            if (expected_type == ParamType::TYPE_BOOL && val.is<bool>()) {
                abi_val.b_val = val.as<bool>();
                effect->set_parameter(key.c_str(), &abi_val);
            } 
            else if (expected_type == ParamType::TYPE_INT && val.is<double>()) { // В Lua все числа double
                abi_val.i_val = val.as<int>();
                effect->set_parameter(key.c_str(), &abi_val);
            } 
            else if (expected_type == ParamType::TYPE_FLOAT && val.is<double>()) {
                abi_val.f_val = static_cast<float>(val.as<double>());
                effect->set_parameter(key.c_str(), &abi_val);
            } 
            else if (expected_type == ParamType::TYPE_VEC3 && val.is<sol::table>()) {
                sol::table t = val.as<sol::table>();
                // Проверяем, что это массив из 3 элементов
                if (t.size() >= 3) {
                    abi_val.vec3_val[0] = t.get_or(1, 0.0f);
                    abi_val.vec3_val[1] = t.get_or(2, 0.0f);
                    abi_val.vec3_val[2] = t.get_or(3, 0.0f);
                    effect->set_parameter(key.c_str(), &abi_val);
                }
            }
            else if (expected_type == ParamType::TYPE_STRING && val.is<std::string>()) {
                temp_str = val.as<std::string>();
                abi_val.s_val = temp_str.c_str(); // Безопасное присвоение
                effect->set_parameter(key.c_str(), &abi_val);
            }
        } catch (const sol::error& e) {
            std::cerr << "Lua Type Error for parameter '" << key << "': " << e.what() << std::endl;
        }
    }
}