#pragma once
#include <sol/sol.hpp>
#include <string>
#include <unordered_set> 
#include "wallpaper-effect.hpp"

#include <sys/timerfd.h> // Для timerfd_create, TFD_NONBLOCK, TFD_CLOEXEC
#include <unistd.h>      // Для close, read
#include <cstring>       // Для strerror
#include <cerrno>        // Для errno

class IDataProvider;
class ICoreContext; // Forward declaration

class LuaEngine {
public:
    LuaEngine() = default;
    ~LuaEngine() { clear_timers(); } // Обязательная очистка при выходе

    bool load();
    bool reload();

    std::string get_active_effect() const;
    bool is_interactive() const;

    void apply_effect_settings(WallpaperEffect* effect, const std::string& effect_name);
    bool configure_provider(IDataProvider* provider);
    
    // Биндинг системного API
    void bind_core_api(ICoreContext* core);

private:
    void clear_timers(); // [NEW] Метод для безопасного удаления таймеров

    sol::state lua;
    std::string config_dir;
    
    ICoreContext* current_core = nullptr;     // [NEW] Запоминаем Ядро для доступа к epoll
    std::unordered_set<int> active_timers;    // [NEW] Храним активные дескрипторы timerfd
    
    std::string get_config_dir();
};