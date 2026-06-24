// src/lua-engine.cpp
#include "lua-engine.hpp"
#include <iostream>
#include <filesystem>
#include <map>

namespace fs = std::filesystem;

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

    // Создаем глобальные таблицы, чтобы скрипты не падали, если вызваны не по порядку
    lua["config"] = lua.create_table();
    lua["core"] = lua.create_table();

    try {
        // 3. Сначала исполняем все автосгенерированные конфиги плагинов
        // Это паттерн conf.d/ - они заполняют дефолтные значения
        if (fs::exists(plugins_dir) && fs::is_directory(plugins_dir)) {
            for (const auto& entry : fs::directory_iterator(plugins_dir)) {
                if (entry.path().extension() == ".lua") {
                    lua.script_file(entry.path().string());
                }
            }
        }

        // 4. Затем исполняем главный init.lua (Логика пользователя)
        // Он может переопределить всё, что было загружено на шаге 3
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