// src/lua-engine.hpp (Заменить секцию public и добавить структуру OutputConfig)

#pragma once
#include <sol/sol.hpp>
#include <string>
#include <unordered_map>
#include <functional>
#include <sys/timerfd.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>

#include "wallpaper-effect.hpp"

class IDataProviderABI;
class ICoreContextABI;

// Структура, описывающая конфигурацию для конкретного монитора
struct OutputConfig {
    std::string effect_name;
    sol::table custom_settings; // Локальные переопределения параметров (если есть)
    float fps_limit = 0.0f;
};

class LuaEngine {
public:
    sol::state& get_state() { return lua; }
    LuaEngine() = default;
    ~LuaEngine() { clear_timers(); }

    std::function<IWallpaperEffectABI*(const std::string&)> get_effect_for_output;


    bool load();
    bool reload();

    std::string get_active_effect() const;
    bool is_interactive() const;

    // [NEW] Запрос конфигурации для конкретного физического монитора
    OutputConfig get_output_config(const std::string& output_name, const std::string& output_desc);

    // [UPDATED] Применение настроек с учетом переопределений для монитора
    void apply_effect_settings(IWallpaperEffectABI* effect, 
                               const std::string& effect_name, 
                               const sol::table& output_specific_settings = sol::nil);

    bool configure_provider(IDataProviderABI* provider);
    void bind_core_api(ICoreContextABI* core);
    void on_frame(float dt, const std::string& output_name);

private:
    void clear_timers();
    void merge_preset_into_target(sol::table target, const sol::table& preset);

    sol::state lua;
    std::string config_dir;
    
    ICoreContextABI* current_core = nullptr;  
    std::unordered_map<int, std::function<void(uint32_t)>> active_timers;
    sol::protected_function frame_callback;
    
    std::string get_config_dir();
};
