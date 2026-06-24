// src/lua-engine.hpp
#pragma once

#include <sol/sol.hpp>
#include <string>
#include "wallpaper-effect.hpp"

class LuaEngine {
public:
    LuaEngine() = default;
    ~LuaEngine() = default;

    /**
     * @brief Инициализирует состояние Lua и загружает скрипты.
     * Загружает все файлы из plugins/*.lua, затем init.lua.
     */
    bool load();

    /**
     * @brief Полностью пересоздает состояние и перезагружает конфиги (Hot-Reload)
     */
    bool reload();

    // Получение базовых настроек Ядра
    std::string get_active_effect() const;
    bool is_interactive() const;

    /**
     * @brief Читает таблицу config[effect_name] и применяет параметры к плагину.
     * Безопасно кастует типы Lua в ожидаемые типы C++.
     */
    void apply_effect_settings(WallpaperEffect* effect, const std::string& effect_name);

private:
    sol::state lua;
    std::string config_dir;

    std::string get_config_dir();
};