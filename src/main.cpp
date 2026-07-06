#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <atomic>
#include <signal.h>
#include <sys/signalfd.h>
#include <algorithm>

#include <nlohmann/json.hpp>
#include "interactive-wallpaper.hpp"
#include "plugin-manager.hpp"
#include "lua-engine.hpp"
#include "lua-config-generator.hpp"

namespace fs = std::filesystem;

// Если мы собираем без установки (в IDE), используем локальные папки как фоллбэк
#ifndef SYSTEM_SOURCE_DIR
#define SYSTEM_SOURCE_DIR "./plugins"
#endif
#ifndef SYSTEM_PLUGIN_DIR
#define SYSTEM_PLUGIN_DIR "./build-release/plugins"
#endif

std::atomic<bool> global_running{true};

// 1. Получение путей для загрузки плагинов
std::vector<std::string> get_plugin_directories() {
    std::vector<std::string> dirs;
    const char* home = getenv("HOME");
    
    // ПРИОРИТЕТ 1: Пользовательские модификации (из ~/.config)
    if (home) {
        dirs.push_back(std::string(home) + "/.config/interactive-wallpaper/effects");
    }
    
    // ПРИОРИТЕТ 2: Локальная папка для разработки (БЕЗ УСТАНОВКИ В СИСТЕМУ)
    // Если запускаем из корня проекта: ./build-release/interactive-wallpaper
    dirs.push_back("./plugins");
    // Если запускаем находясь внутри папки build: ./interactive-wallpaper
    dirs.push_back("../plugins");
    
    // ПРИОРИТЕТ 3: Системные скомпилированные плагины (/usr/local/lib/...)
    dirs.push_back(SYSTEM_PLUGIN_DIR);
    
    return dirs;
}

// 2. Функция копирования исходников
void init_user_workspace() {
    const char* home = getenv("HOME");
    if (!home) return;

    fs::path user_effects_dir = fs::path(home) / ".config/interactive-wallpaper/effects";
    
    // Ищем папку с исходниками: локальную или системную
    fs::path source_to_use;
    if (fs::exists("./plugins")) {
        source_to_use = "./plugins";
    } else if (fs::exists("../plugins")) {
        source_to_use = "../plugins";
    } else if (fs::exists(SYSTEM_SOURCE_DIR)) {
        source_to_use = SYSTEM_SOURCE_DIR;
    } else {
        std::cerr << "Error: Source directory not found. Run from project root or install." << std::endl;
        return;
    }

    std::cout << "Initializing modding workspace at: " << user_effects_dir << std::endl;
    std::cout << "Copying templates from: " << source_to_use << std::endl;
    fs::create_directories(user_effects_dir);

    // Копируем исходники, не перезаписывая уже измененные пользователем файлы
    auto copy_opts = fs::copy_options::recursive | fs::copy_options::skip_existing;
    std::error_code ec;
    
    fs::copy(source_to_use, user_effects_dir, copy_opts, ec);
    
    if (ec) {
        std::cerr << "Failed to copy workspace: " << ec.message() << std::endl;
    } else {
        std::cout << "Workspace initialized successfully!\n";
        std::cout << "You can now edit .cpp and .glsl files in " << user_effects_dir << " and compile them." << std::endl;
    }
}

int main(int argc, char** argv) {
    // 1. Handle CLI commands (e.g., initial configuration generation)
    if (argc > 1) {
        std::string arg1 = argv[1];
        
        if (arg1 == "--init-workspace") {
            init_user_workspace();
            return 0;
        }

        if (arg1 == "--init-config" || arg1 == "--list-plugins" || arg1 == "--list-providers" || arg1 == "--inspect") {
            // ИСПРАВЛЕНИЕ: Вызываем правильную функцию и передаем вектор в PluginManager
            std::vector<std::string> p_dirs = get_plugin_directories();
            PluginManager pm(p_dirs);
            pm.discover_plugins();

            if (arg1 == "--init-config") {
                LuaConfigGenerator::generate_configs(pm);
                return 0;
            } 
            else if (arg1 == "--list-plugins") {
                nlohmann::json j = pm.get_available_effects();
                std::cout << j.dump(2) << std::endl;
                return 0;
            } 
            else if (arg1 == "--list-providers") {
                nlohmann::json j = pm.get_available_providers();
                std::cout << j.dump(2) << std::endl;
                return 0;
            } 
            else if (arg1 == "--inspect") {
                if (argc < 3) {
                    std::cerr << "Usage: interactive-wallpaper --inspect \"<Plugin Name>\"" << std::endl;
                    return 1;
                }
                nlohmann::json info = pm.inspect_plugin(argv[2]);
                if (info.is_null()) {
                    std::cerr << "Error: Plugin '" << argv[2] << "' not found." << std::endl;
                    return 1;
                }
                std::cout << info.dump(2) << std::endl;
                return 0;
            }
        }
    } 

    // 2. Block POSIX signals. They will be handled safely via signalfd integrated into the epoll loop.
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    if (sigprocmask(SIG_BLOCK, &mask, nullptr) == -1) {
        std::cerr << "Failed to block POSIX signals." << std::endl;
        return 1;
    }

    // 3. Initialize the embedded Lua runtime
    LuaEngine lua_engine;

    // 4. CRITICAL RAII ORDER: Initialize PluginManager FIRST.
    // When main() exits, C++ destroys local variables in reverse order of declaration.
    // By declaring PluginManager before InteractiveWallpaper, we guarantee that dlclose() 
    // is called only AFTER the microkernel has safely destroyed all active plugin instances.
    std::vector<std::string> plugin_dirs = get_plugin_directories();
    PluginManager plugin_manager(plugin_dirs);
    plugin_manager.discover_plugins();

    auto available_effects = plugin_manager.get_available_effects();
    if (available_effects.empty()) {
        // ИСПРАВЛЕНИЕ: Безопасный вывод в лог (plugin_dir ранее не существовала в этой области видимости)
        std::cerr << "No visual plugins discovered in searched directories. Exiting." << std::endl;
        return 1;
    }

    // 5. Initialize Core context (second in RAII order)
    WallpaperConfig wallpaper_config;
    wallpaper_config.output_name = "*";
    wallpaper_config.interactive = true;
    
    InteractiveWallpaper wallpaper(wallpaper_config, lua_engine);

    // 6. Bind Core C-ABI to the Lua runtime BEFORE loading scripts, allowing timers and hooks to register
    lua_engine.bind_core_api(&wallpaper);

    // 7. Load user configuration from ~/.config/interactive-wallpaper/init.lua
    if (!lua_engine.load()) {
        std::cerr << "Failed to load Lua configuration. Proceeding with fallback defaults." << std::endl;
    }

    // Select the default fallback effect if the configured one is missing
    std::string effect_name = lua_engine.get_active_effect();
    if (effect_name.empty() || std::find(available_effects.begin(), available_effects.end(), effect_name) == available_effects.end()) {
        effect_name = available_effects[0];
    }
    std::cout << "Selected fallback visual effect: '" << effect_name << "'" << std::endl;

    wallpaper.set_plugin_manager(&plugin_manager, effect_name);
    
    // 8. Connect to the Wayland display and bind compositor interfaces (wl_compositor, layer-shell, etc.)
    if (!wallpaper.initialize()) {
        std::cerr << "CRITICAL: Failed to connect to the Wayland compositor." << std::endl;
        return 1;
    }

    // 9. Initialize and start all active Data Providers (Audio, Input, etc.) based on Lua settings
    plugin_manager.initialize_providers(&wallpaper, [&lua_engine](IDataProviderABI* p) {
        return lua_engine.configure_provider(p);
    });

    // 10. Enter the zero-latency epoll event loop
    std::cout << "Entering main epoll event loop..." << std::endl;
    wallpaper.run();
    
    std::cout << "Microkernel shut down gracefully. Clean RAII resource deallocation in progress..." << std::endl;
    return 0;
}