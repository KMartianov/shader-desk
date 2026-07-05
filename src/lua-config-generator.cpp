// src/lua-config-generator.cpp
#include "lua-config-generator.hpp"
#include "embedded_ctl.hpp"   
#include "embedded_init.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <map>

namespace fs = std::filesystem;

// ==============================================================================
// HELPER FUNCTIONS
// ==============================================================================

std::string LuaConfigGenerator::get_config_dir() {
    const char* xdg_config = std::getenv("XDG_CONFIG_HOME");
    std::string base_dir;
    if (xdg_config && *xdg_config) {
        base_dir = std::string(xdg_config);
    } else {
        base_dir = std::string(std::getenv("HOME")) + "/.config";
    }
    return base_dir + "/interactive-wallpaper";
}

// Converts "Ico Sphere Effect" to "ico_sphere_effect"
std::string LuaConfigGenerator::sanitize_filename(std::string name) {
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
        if (std::isspace(c) || c == '-') return '_';
        return (char)std::tolower(c);
    });
    return name;
}

// Converts C-ABI values (ParamValueABI) to Lua syntax
// [NEW] This function now works exclusively with flat C-structs, not std::variant.
std::string LuaConfigGenerator::value_to_lua_string(const ParamValueABI& val) {
    switch (val.type) {
        case ParamType::TYPE_BOOL: 
            return val.b_val ? "true" : "false";
        case ParamType::TYPE_INT: 
            return std::to_string(val.i_val);
        case ParamType::TYPE_FLOAT: 
            return std::to_string(val.f_val);
        case ParamType::TYPE_VEC3: 
            return "{" + std::to_string(val.vec3_val[0]) + ", " + 
                         std::to_string(val.vec3_val[1]) + ", " + 
                         std::to_string(val.vec3_val[2]) + "}";
        case ParamType::TYPE_STRING: 
            return std::string("\"") + val.s_val + "\""; // Wrap in quotes for Lua
    }
    return "nil";
}

// Trims whitespace from string edges
static std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t");
    if (std::string::npos == first) return "";
    size_t last = str.find_last_not_of(" \t");
    return str.substr(first, (last - first + 1));
}

// ==============================================================================
// MAIN GENERATION LOGIC
// ==============================================================================

void LuaConfigGenerator::generate_configs(PluginManager& pm) {
    std::string config_dir = get_config_dir();
    fs::path plugins_dir = fs::path(config_dir) / "plugins";

    // Create directories if they do not exist
    fs::create_directories(plugins_dir);

    auto available_effects = pm.get_available_effects();
    if (available_effects.empty()) {
        std::cerr << "Warning: No visual plugins found. Nothing to configure." << std::endl;
        return;
    }

    std::cout << "Generating Lua configs in: " << config_dir << std::endl;

    // 1. Создаем ctl.lua (если не существует) <--- НОВЫЙ БЛОК!
    fs::path ctl_lua_path = fs::path(config_dir) / "ctl.lua";
    if (!fs::exists(ctl_lua_path)) {
        generate_ctl_lua(ctl_lua_path.string());
        std::cout << "  ✓ Generated auxiliary CLI module: ctl.lua" << std::endl;
    }

    // 2. Создаем init.lua (если не существует)
    fs::path init_lua_path = fs::path(config_dir) / "init.lua";
    if (!fs::exists(init_lua_path)) {
        generate_init_lua(init_lua_path.string(), available_effects[0]);
        std::cout << "  ✓ Generated main config: init.lua" << std::endl;
    }

    // 2. Iterate through plugins to update/create their individual configs
    for (const auto& effect_name : available_effects) {
        auto effect = pm.create_effect(effect_name);
        if (!effect) continue;

        std::string filename = sanitize_filename(effect_name) + ".lua";
        fs::path plugin_filepath = plugins_dir / filename;

        update_plugin_config(plugin_filepath.string(), effect_name, effect.get());
        std::cout << "  ✓ Processed config for: " << effect_name << std::endl;
    }

    std::cout << "Config generation completed successfully!" << std::endl;
}


void LuaConfigGenerator::generate_ctl_lua(const std::string& filepath) {
    std::ofstream out(filepath);
    out << Embedded::CTL_LUA; 
    out.close();
}

void LuaConfigGenerator::generate_init_lua(const std::string& filepath, const std::string& default_effect) {
    std::ofstream out(filepath);
    out << Embedded::INIT_LUA;
    out.close();
}

void LuaConfigGenerator::update_plugin_config(const std::string& filepath, const std::string& plugin_name, IWallpaperEffectABI* effect) {
    std::map<std::string, std::string> user_values;
    std::vector<std::string> user_logic;

    // --- STEP 1: Parse existing file (if any) ---
    if (fs::exists(filepath)) {
        std::ifstream in(filepath);
        std::string line;
        bool in_managed = false;
        
        while (std::getline(in, line)) {
            if (line.find("<<< BEGIN MANAGED PARAMS >>>") != std::string::npos) { 
                in_managed = true; 
                continue; 
            }
            if (line.find("<<< END MANAGED PARAMS >>>") != std::string::npos) { 
                in_managed = false; 
                continue; 
            }

            if (in_managed) {
                // Looking for pattern: p.key = value -- comment
                // This is a primitive but reliable parser for our needs.
                auto dot_pos = line.find("p.");
                auto eq_pos = line.find("=");
                
                if (dot_pos != std::string::npos && eq_pos != std::string::npos && dot_pos < eq_pos) {
                    std::string key = trim(line.substr(dot_pos + 2, eq_pos - (dot_pos + 2)));
                    std::string rest = line.substr(eq_pos + 1);
                    
                    // Strip comments from the value (looking for '--')
                    auto comment_pos = rest.find("--");
                    std::string value;
                    if (comment_pos != std::string::npos) {
                        value = trim(rest.substr(0, comment_pos));
                    } else {
                        value = trim(rest);
                    }
                    
                    // If the value ends with a trailing comma, remove it
                    if (!value.empty() && value.back() == ',') value.pop_back();

                    user_values[key] = value;
                }
            } else {
                // Save user's free logic (skipping the template header)
                if (line.find("config[\"") == std::string::npos && line.find("local p =") == std::string::npos) {
                    user_logic.push_back(line);
                }
            }
        }
    }

    // --- STEP 2: Generate updated file ---
    std::ofstream out(filepath);
    
    // Standard header
    out << "config[\"" << plugin_name << "\"] = config[\"" << plugin_name << "\"] or {}\n";
    out << "local p = config[\"" << plugin_name << "\"]\n\n";
    
    out << "-- ==============================================================================\n";
    out << "-- <<< BEGIN MANAGED PARAMS >>>\n";
    out << "-- Не удаляйте маркеры. Изменяйте значения справа от знака '='.\n";
    out << "-- ==============================================================================\n";

    // Write actual parameters from C++
    // [NEW] Use safe C-ABI to get the plugin parameter list.
    uint32_t count = effect->get_parameter_count();
    for (uint32_t i = 0; i < count; ++i) {
        ParamInfoABI param_info;
        effect->get_parameter_info(i, &param_info);
        
        std::string default_val = value_to_lua_string(param_info.default_value);
        std::string param_name(param_info.name);

        out << "p." << param_name << " = ";
        
        if (user_values.count(param_name)) {
            // Restore user-modified value
            out << user_values[param_name] << " -- " << param_info.description << "\n";
            user_values.erase(param_name); // Удаляем, чтобы найти DEPRECATED
        } else {
            // Insert new default value
            out << default_val << " -- [NEW] " << param_info.description << "\n";
        }
    }

    // Remainder in user_values no longer exists in the plugin (developer removed or renamed it)
    for (const auto& [dep_key, dep_val] : user_values) {
        // Prevent duplicate DEPRECATED markers
        if (dep_key.find("[DEPRECATED]") == std::string::npos) {
            out << "-- [DEPRECATED] p." << dep_key << " = " << dep_val << " -- Удалено в новой версии\n";
        }
    }

    out << "-- <<< END MANAGED PARAMS >>>\n";
    out << "-- ==============================================================================\n";

    // --- STEP 3: Restore user's custom logic ---
    // Remove empty lines at the beginning of the saved logic
    while (!user_logic.empty() && trim(user_logic.front()).empty()) {
        user_logic.erase(user_logic.begin());
    }

    if (!user_logic.empty()) {
        for (const auto& line : user_logic) {
            out << line << "\n";
        }
    } else {
        out << "\n-- ЗОНА СВОБОДНОЙ ЛОГИКИ ПЛАГИНА:\n";
        out << "-- Пишите здесь функции и переопределения, специфичные для этого плагина.\n";
        out << "-- Пример загрузки пресета (раскомментируйте, если пресет существует):\n";
        out << "-- core.utils.apply_preset(p, \"" << plugin_name << "\", \"default\")\n";
    }

    out.close();
}