// src/main.cpp
#include "interactive-wallpaper.hpp"
#include "plugin-manager.hpp"
#include "lua-engine.hpp"
#include "lua-config-generator.hpp"
#include <iostream>
#include <string>
#include <atomic>
#include <signal.h>
#include <sys/signalfd.h>
#include <filesystem>
#include <algorithm>

std::atomic<bool> global_running{true};

std::string get_plugin_directory() {
    const char* home = getenv("HOME");
    return home ? std::string(home) + "/.config/interactive-wallpaper/effects" : "./effects";
}

int main(int argc, char** argv) {
    // 1. Handle CLI commands (e.g., initial configuration generation)
    if (argc > 1 && std::string(argv[1]) == "--init-config") {
        std::string plugin_dir = get_plugin_directory();
        PluginManager pm(plugin_dir);
        pm.discover_plugins();
        LuaConfigGenerator::generate_configs(pm);
        return 0; // Exit cleanly without connecting to Wayland
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
    std::string plugin_dir = get_plugin_directory();
    PluginManager plugin_manager(plugin_dir);
    plugin_manager.discover_plugins();

    auto available_effects = plugin_manager.get_available_effects();
    if (available_effects.empty()) {
        std::cerr << "No visual plugins discovered in: " << plugin_dir << ". Exiting." << std::endl;
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