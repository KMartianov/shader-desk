#pragma once
#include <sol/sol.hpp>
#include <string>
#include <unordered_set> 
#include "wallpaper-effect.hpp"

#include <sys/timerfd.h> // For timerfd_create, TFD_NONBLOCK, TFD_CLOEXEC
#include <unistd.h>      // For close, read
#include <cstring>       // For strerror
#include <cerrno>        // For errno

class IDataProviderABI;
class ICoreContextABI; // Forward declaration for ABI interfaces

class LuaEngine {
public:
    LuaEngine() = default;
    ~LuaEngine() { clear_timers(); } // Mandatory cleanup on destruction

    bool load();
    bool reload();

    std::string get_active_effect() const;
    bool is_interactive() const;

    // [NEW] Replaced parameter types with ABI interfaces
    void apply_effect_settings(IWallpaperEffectABI* effect, const std::string& effect_name);
    bool configure_provider(IDataProviderABI* provider);
    
    // System API binding
    void bind_core_api(ICoreContextABI* core);

private:
    void clear_timers(); // [NEW] Method for safely clearing timers

    sol::state lua;
    std::string config_dir;
    
    // [NEW] Pointer to ABI core for epoll access
    ICoreContextABI* current_core = nullptr;  
    std::unordered_map<int, std::function<void(uint32_t)>> active_timers;  // [NEW] Store active timerfd descriptors
    
    std::string get_config_dir();
};