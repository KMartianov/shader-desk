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
    
    // БЕЗОПАСНОЕ чтение таблицы core
    sol::object core_obj = lua["core"];
    if (!core_obj.is<sol::table>()) return;
    sol::table core_table = core_obj.as<sol::table>();
    
    // Безопасно создаем или получаем таблицу debug
    if (!core_table["debug"].is<sol::table>()) {
        core_table["debug"] = lua.create_table();
    }
    sol::table debug = core_table["debug"];
    
    debug["dump_blackboard"] = [core]() {
        std::cout << "\n=== BLACKBOARD DUMP ===\n";
        auto keys = core->get_blackboard().get_all_keys();
        if (keys.empty()) {
            std::cout << "  (BlackBoard is empty)\n";
        } else {
            for (const auto& key : keys) {
                float val = *core->get_blackboard().bind_float(key);
                std::cout << "  - " << key << " : " << val << "\n";
            }
        }
        std::cout << "=======================\n\n";
    };
}

bool LuaEngine::reload() {
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
        // 0: bool, 1: int, 2: float, 3: glm::vec3
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
        } catch (const sol::error& e) {
            std::cerr << "Lua Type Error for parameter '" << key << "': " << e.what() << std::endl;
        }
    }
}