// src/lua-engine.hpp
#pragma once

#include <sol/sol.hpp>
#include <string>
#include "wallpaper-effect.hpp"

class IDataProvider;

class LuaEngine {
public:
    LuaEngine() = default;
    ~LuaEngine() = default;

    bool load();
    bool reload();

    std::string get_active_effect() const;
    bool is_interactive() const;

    void apply_effect_settings(WallpaperEffect* effect, const std::string& effect_name);

    // --- НОВЫЕ МЕТОДЫ ДЛЯ ПРОВАЙДЕРОВ И ЯДРА ---

    /**
     * @brief Читает таблицу core.providers[name] и применяет настройки.
     * @return true, если провайдер включен (enabled != false), иначе false.
     */
    bool configure_provider(IDataProvider* provider);

    /**
     * @brief Биндит системные API (например, core.debug.dump_blackboard), 
     * которым нужен доступ к экземпляру Ядра (ICoreContext).
     */
    void bind_core_api(ICoreContext* core);

private:
    sol::state lua;
    std::string config_dir;

    std::string get_config_dir();
};