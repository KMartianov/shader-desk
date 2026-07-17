// Src/lua-engine.hpp (Replace the public section and add the OutputConfig struct)

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

// Individual layer configuration 
struct LayerConfig {
    std::string effect_name;
    std::string tag;
    sol::table custom_settings;
    bool is_postprocess = false; // Post-processing flag
    bool clear_depth = true;
};

// Structure describing the configuration for a specific monitor
struct OutputConfig {
    std::vector<LayerConfig> layers;
    float fps_limit = 0.0f;
    float fbo_scale = 1.0f; // Optimization: render at a lower resolution
};


class LuaEngine {
public:
    sol::state& get_state() { return lua; }
    LuaEngine() = default;
    ~LuaEngine() { clear_timers(); }

    std::function<IWallpaperEffectABI*(const std::string&, const std::string&)> get_layer_by_tag;
    void set_config_dir(const std::string& dir) { config_dir = dir; }


    bool load();
    bool reload();

    std::string get_active_effect() const;
    bool is_interactive() const;

    // Request configuration for a specific physical monitor
    OutputConfig get_output_config(const std::string& output_name, const std::string& output_desc);
    


    // Apply settings considering monitor-specific overrides
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
