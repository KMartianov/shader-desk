#pragma once
#include <sol/sol.hpp>
#include <string>
#include <unordered_set> 
#include "wallpaper-effect.hpp"

#include <sys/timerfd.h> // Для timerfd_create, TFD_NONBLOCK, TFD_CLOEXEC
#include <unistd.h>      // Для close, read
#include <cstring>       // Для strerror
#include <cerrno>        // Для errno

class IDataProviderABI;
class ICoreContextABI; // Forward declaration для ABI-интерфейсов

class LuaEngine {
public:
    LuaEngine() = default;
    ~LuaEngine() { clear_timers(); } // Обязательная очистка при выходе

    bool load();
    bool reload();

    std::string get_active_effect() const;
    bool is_interactive() const;

    // [NEW] Заменили типы параметров на ABI интерфейсы
    void apply_effect_settings(IWallpaperEffectABI* effect, const std::string& effect_name);
    bool configure_provider(IDataProviderABI* provider);
    
    // Биндинг системного API
    void bind_core_api(ICoreContextABI* core);

private:
    void clear_timers(); // [NEW] Метод для безопасного удаления таймеров

    sol::state lua;
    std::string config_dir;
    
    // [NEW] Указатель на ABI ядро для доступа к epoll
    ICoreContextABI* current_core = nullptr;  
    std::unordered_map<int, std::function<void(uint32_t)>> active_timers;  // [NEW] Храним активные дескрипторы timerfd
    
    std::string get_config_dir();
};