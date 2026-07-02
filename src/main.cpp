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
    // 1. Handle CLI commands (Config generation and preset copying)
    if (argc > 1 && std::string(argv[1]) == "--init-config") {
        std::string plugin_dir = get_plugin_directory();
        PluginManager pm(plugin_dir);
        pm.discover_plugins();
        LuaConfigGenerator::generate_configs(pm);
        
        // Automatically copy presets from plugin folders to ~/.config/...
        try {
            namespace fs = std::filesystem;
            fs::path target_presets = std::string(getenv("HOME")) + "/.config/interactive-wallpaper/presets";
            fs::create_directories(target_presets);
            
            // Find all "presets" folders inside the plugin installation directory
            for (const auto& entry : fs::recursive_directory_iterator(plugin_dir)) {
                if (entry.is_directory() && entry.path().filename() == "presets") {
                    // Parent folder name (plugin name, e.g., ico-sphere-effect)
                    std::string plugin_folder_name = entry.path().parent_path().filename().string();
                    // Ensure sanitize_plugin_name (underscores) works correctly:
                    std::replace(plugin_folder_name.begin(), plugin_folder_name.end(), '-', '_');
                    
                    fs::path dest = target_presets / plugin_folder_name;
                    // Copy and update existing files
                    fs::copy(entry.path(), dest, fs::copy_options::recursive | fs::copy_options::update_existing);
                    std::cout << "Copied presets for: " << plugin_folder_name << std::endl;
                }
            }
        } catch(const std::exception& e) {
            std::cerr << "Failed to copy presets: " << e.what() << std::endl;
        }

        return 0; // Exit, Wayland is not needed here
    }

    // 2. Block signals (They will be handled via signalfd in epoll)
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    if (sigprocmask(SIG_BLOCK, &mask, nullptr) == -1) {
        std::cerr << "Failed to block signals" << std::endl;
        return 1;
    }

    // 3. Create Lua engine (do NOT load scripts yet!)
    LuaEngine lua_engine;

    // 4. Setup basic parameters and create Core
    // IMPORTANT (RAII): Core is declared BEFORE plugins. 
    // It will outlive them and be destroyed last.
    WallpaperConfig wallpaper_config;
    wallpaper_config.output_name = "*";
    wallpaper_config.interactive = true; // Placeholder, real value read later
    
    InteractiveWallpaper wallpaper(wallpaper_config, lua_engine);

    // 5. Bind Core API to Lua (core.set_interval, core.set_string, etc. are now available)
    // Do this BEFORE loading files so scripts can use timers immediately!
    lua_engine.bind_core_api(&wallpaper);

    // 6. NOW safely load configuration via Lua
    if (!lua_engine.load()) {
        std::cerr << "Failed to load Lua configuration!" << std::endl;
        // Do not exit, try to start with defaults
    }

    // 7. Plugin manager initialization
    // IMPORTANT (RAII): Upon exiting main, this will be destroyed FIRST, 
    // gracefully unregistering from the still-alive Core (epoll/BlackBoard).
    PluginManager plugin_manager(get_plugin_directory());
    plugin_manager.discover_plugins();

    auto available_effects = plugin_manager.get_available_effects();
    if (available_effects.empty()) {
        std::cerr << "No visual plugins found! Exiting." << std::endl;
        return 1;
    }

    // Select effect: from Lua config or fallback to the first found
    std::string effect_name = lua_engine.get_active_effect();
    if (effect_name.empty() || std::find(available_effects.begin(), available_effects.end(), effect_name) == available_effects.end()) {
        effect_name = available_effects[0];
    }
    std::cout << "Selected effect: '" << effect_name << "'" << std::endl;

    wallpaper.set_plugin_manager(&plugin_manager, effect_name);
    
    // 8. Connect to Wayland and start providers
    if (!wallpaper.initialize()) {
        std::cerr << "Failed to initialize Wayland compositor connection" << std::endl;
        return 1;
    }

    // Initialize providers, passing a lambda to apply Lua settings
    plugin_manager.initialize_providers(&wallpaper, [&lua_engine](IDataProviderABI* p) {
        return lua_engine.configure_provider(p);
    });

    // 9. Main Event Loop (Zero-Latency Epoll Based)
    std::cout << "Starting wallpaper main loop..." << std::endl;
    wallpaper.run();
    
    return 0;
}