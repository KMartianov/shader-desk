// src/lua-config-generator.hpp
#pragma once

#include "plugin-manager.hpp"

class LuaConfigGenerator {
public:
    /**
     * @brief Запускает процесс генерации/обновления Lua конфигураций.
     * Сканирует все плагины, извлекает их параметры и применяет
     * паттерн "Гибридный конфиг" для файлов в ~/.config/interactive-wallpaper/plugins/
     * 
     * @param pm Инициализированный менеджер плагинов с загруженными .so
     */
    static void generate_configs(PluginManager& pm);

private:
    static std::string get_config_dir();
    static std::string sanitize_filename(std::string name);
    static std::string value_to_lua_string(const ParamValueABI& val);
    
    static void generate_init_lua(const std::string& filepath, const std::string& default_effect);
    static void generate_ctl_lua(const std::string& filepath);
    static void update_plugin_config(const std::string& filepath, const std::string& plugin_name, IWallpaperEffectABI* effect);
};