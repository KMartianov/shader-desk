// Src/lua-config-generator.hpp
#pragma once

#include "plugin-manager.hpp"

class LuaConfigGenerator {
public:
    /**
     * @brief Starts the generation/update process for Lua configurations.
     * Scans all plugins, extracts their parameters, and applies
     * the "Hybrid Config" pattern for files in ~/.config/interactive-wallpaper/plugins/
     * 
     * @param pm Initialized plugin manager with loaded .so libraries
     */
    static void generate_configs(PluginManager& pm, const std::string& custom_dir = "");


private:
    static std::string get_config_dir(const std::string& custom_dir);
    static std::string sanitize_filename(std::string name);
    static std::string value_to_lua_string(const ParamValueABI& val);
    
    static void generate_init_lua(const std::string& filepath, const std::string& default_effect);
    static void generate_ctl_lua(const std::string& filepath);
    static void update_plugin_config(const std::string& filepath, const std::string& plugin_name, IWallpaperEffectABI* effect);
};