#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <atomic>
#include <signal.h>
#include <sys/signalfd.h>
#include <algorithm>
#include <sys/file.h>

#include <nlohmann/json.hpp>
#include "interactive-wallpaper.hpp"
#include "plugin-manager.hpp"
#include "lua-engine.hpp"
#include "lua-config-generator.hpp"

// Include the compile-time embedded help text
#include "embedded_help.hpp"

namespace fs = std::filesystem;

// Fallbacks for local IDE builds if CMake didn't define system paths
#ifndef SYSTEM_SOURCE_DIR
#define SYSTEM_SOURCE_DIR "./plugins"
#endif
#ifndef SYSTEM_PLUGIN_DIR
#define SYSTEM_PLUGIN_DIR "./build-release/plugins"
#endif

std::atomic<bool> global_running{true};

// 1. Get directories for plugin discovery
std::vector<std::string> get_plugin_directories(const std::string& custom_config_dir) {
    std::vector<std::string> dirs;
    
    // PRIORITY 1: Workspace (Custom via --config or standard ~/.config)
    if (!custom_config_dir.empty()) {
        dirs.push_back(custom_config_dir + "/effects");
    } else {
        const char* home = getenv("HOME");
        if (home) {
            dirs.push_back(std::string(home) + "/.config/interactive-wallpaper/effects");
        }
    }
    
    // PRIORITY 2: Local build (Running directly from the repository)
    #ifdef LOCAL_PLUGIN_DIR
    dirs.push_back(LOCAL_PLUGIN_DIR);
    #endif
    
    // Return hardcoded paths for different build profiles (Tracy, Release, Debug)
    dirs.push_back("./build-tracy/plugins");
    dirs.push_back("./build-release/plugins");
    dirs.push_back("./build/plugins");
    dirs.push_back("./plugins");
    dirs.push_back("../plugins");
    
    // PRIORITY 3: System installation FHS (/usr/lib)
    #ifdef SYSTEM_PLUGIN_DIR
    dirs.push_back(SYSTEM_PLUGIN_DIR);
    #endif
    
    return dirs;
}

// 2. Initialize user modding workspace
void init_user_workspace(const std::string& custom_config_dir) {
    std::string config_base = custom_config_dir.empty() ? 
        (std::string(getenv("HOME")) + "/.config/interactive-wallpaper") : custom_config_dir;
        
    fs::path user_effects_dir = fs::path(config_base) / "effects";
    
    fs::path source_to_use;
    if (fs::exists("./plugins")) source_to_use = "./plugins";
    else if (fs::exists("../plugins")) source_to_use = "../plugins";
    else if (fs::exists(SYSTEM_SOURCE_DIR)) source_to_use = SYSTEM_SOURCE_DIR;
    else {
        std::cerr << "[Error] Source directory not found." << std::endl;
        return;
    }

    std::cout << "Initializing workspace at: " << user_effects_dir << std::endl;
    fs::create_directories(user_effects_dir);

    auto copy_opts = fs::copy_options::recursive | fs::copy_options::skip_existing;
    std::error_code ec;
    fs::copy(source_to_use, user_effects_dir, copy_opts, ec);
    
    if (ec) {
        std::cerr << "[Error] Failed to copy workspace sources: " << ec.message() << std::endl;
        return;
    }

    // Inject compiled .so files into the new bundles
    std::vector<std::string> search_dirs = get_plugin_directories(custom_config_dir);
    for (const auto& entry : fs::directory_iterator(user_effects_dir)) {
        if (!entry.is_directory()) continue;
        
        std::string plugin_name = entry.path().filename().string();
        std::string so_filename = plugin_name + ".so";
        
        for (const auto& dir : search_dirs) {
            fs::path so_nested = fs::path(dir) / plugin_name / so_filename;
            fs::path so_flat = fs::path(dir) / so_filename;

            fs::path found_so = fs::exists(so_nested) ? so_nested : (fs::exists(so_flat) ? so_flat : fs::path(""));

            if (!found_so.empty()) {
                fs::copy_file(found_so, entry.path() / so_filename, fs::copy_options::update_existing, ec);
                if (!ec) std::cout << "  ✓ Embedded compiled binary: " << so_filename << std::endl;
                break;
            }
        }
    }
    std::cout << "\n✓ Workspace ready! Edit .glsl files for instant hot-reload.\n";
}

int main(int argc, char** argv) {
    // 1. Flexible CLI Argument Parsing
    std::string custom_config_dir = "";
    std::string main_command = "";
    std::string inspect_target = "";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        // --- HELP FLAG ---
        if (arg == "-h" || arg == "--help") {
            std::cout << Embedded::HELP_TEXT << std::endl;
            return 0; // Print embedded help and exit cleanly
        } 
        else if (arg == "--config" && i + 1 < argc) {
            // CRITICAL: Convert to absolute path immediately.
            // If run via Hyprland/Sway "exec-once", relative paths will point to $HOME.
            custom_config_dir = fs::absolute(argv[i + 1]).string();
            i++; // Skip the path argument
        } 
        else if (arg == "--inspect" && i + 1 < argc) {
            main_command = arg;
            inspect_target = argv[i + 1];
            i++; // Skip the plugin name argument
        } 
        else if (arg == "--init-workspace" || arg == "--init-config" || 
                 arg == "--list-plugins" || arg == "--list-providers") {
            // Strict white-list for utility commands
            main_command = arg; 
        }
        else {
            // --- UNKNOWN ARGUMENT PROTECTION ---
            // Catch typos (e.g. "--h"), garbage strings, or missing values
            std::cerr << "\033[31m[Error] Unknown command or argument: '" << arg << "'\033[0m\n\n";
            std::cout << Embedded::HELP_TEXT << std::endl;
            return 1; // Exit with error code
        }
    }

    // 2. Handle specific CLI actions and exit
    if (!main_command.empty()) {
        if (main_command == "--init-workspace") {
            init_user_workspace(custom_config_dir);
            return 0;
        }

        if (main_command == "--init-config" || main_command == "--list-plugins" || 
            main_command == "--list-providers" || main_command == "--inspect") {
            
            std::vector<std::string> p_dirs = get_plugin_directories(custom_config_dir);

            PluginManager pm(p_dirs);
            pm.discover_plugins();

            if (main_command == "--init-config") {
                LuaConfigGenerator::generate_configs(pm, custom_config_dir);
                return 0;
            } 
            else if (main_command == "--list-plugins") {
                nlohmann::json j = pm.get_available_effects();
                std::cout << j.dump(2) << std::endl;
                return 0;
            } 
            else if (main_command == "--list-providers") {
                nlohmann::json j = pm.get_available_providers();
                std::cout << j.dump(2) << std::endl;
                return 0;
            } 
            else if (main_command == "--inspect") {
                if (inspect_target.empty()) {
                    std::cerr << "Usage: interactive-wallpaper --inspect \"<Plugin Name>\"" << std::endl;
                    return 1;
                }
                nlohmann::json info = pm.inspect_plugin(inspect_target);
                if (info.is_null()) {
                    std::cerr << "[Error] Plugin '" << inspect_target << "' not found." << std::endl;
                    return 1;
                }
                std::cout << info.dump(2) << std::endl;
                return 0;
            }
        }
    } 

    // 2.5 Single Instance Protection
    // Prevent multiple instances of the microkernel from clashing over UNIX sockets.
    std::string lock_file = "/tmp/shader-desk-" + std::to_string(getuid()) + ".lock";
    int lock_fd = open(lock_file.c_str(), O_CREAT | O_RDWR, 0600);
    if (lock_fd < 0 || flock(lock_fd, LOCK_EX | LOCK_NB) < 0) {
        std::cerr << "\033[31m[Error] Shader Desk is already running for this user.\033[0m\n";
        std::cerr << "Close the existing instance before starting a new one.\n";
        if (lock_fd >= 0) close(lock_fd);
        return 1;
    }
    // The lock is held automatically until the process exits or crashes.

    // 3. Block POSIX signals. 
    // They will be handled safely via signalfd integrated into the Wayland epoll loop.
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    if (sigprocmask(SIG_BLOCK, &mask, nullptr) == -1) {
        std::cerr << "[Error] Failed to block POSIX signals." << std::endl;
        return 1;
    }

    // 4. Initialize the embedded Lua runtime and configure custom directory
    LuaEngine lua_engine;
    if (!custom_config_dir.empty()) {
        lua_engine.set_config_dir(custom_config_dir);
        std::cout << "[Core] Using custom config directory: " << custom_config_dir << std::endl;
    }

    // 5. CRITICAL RAII ORDER: Initialize PluginManager FIRST.
    // When main() exits, C++ destroys local variables in reverse declaration order.
    // By declaring PluginManager before InteractiveWallpaper, we guarantee that dlclose() 
    // Is called only AFTER the Wayland microkernel has safely destroyed all visual effects.
    std::vector<std::string> plugin_dirs = get_plugin_directories(custom_config_dir);
    PluginManager plugin_manager(plugin_dirs);
    plugin_manager.discover_plugins();

    auto available_effects = plugin_manager.get_available_effects();
    if (available_effects.empty()) {
        std::cerr << "\033[31mCRITICAL: No visual plugins discovered. Exiting.\033[0m" << std::endl;
        return 1;
    }

    // 6. Initialize Core context (second in RAII order)
    WallpaperConfig wallpaper_config;
    wallpaper_config.output_name = "*";
    wallpaper_config.interactive = true;
    
    InteractiveWallpaper wallpaper(wallpaper_config, lua_engine);

    // 7. Bind Core C-ABI to the Lua runtime BEFORE loading scripts (allows timers to register)
    lua_engine.bind_core_api(&wallpaper);

    // 8. Load user configuration
    if (!lua_engine.load()) {
        std::cerr << "\033[33m[Warning] Failed to evaluate Lua configuration. Proceeding with fallback defaults.\033[0m" << std::endl;
    }

    // 9. Select the default fallback effect if the configured one is missing
    std::string effect_name = lua_engine.get_active_effect();
    if (effect_name.empty() || std::find(available_effects.begin(), available_effects.end(), effect_name) == available_effects.end()) {
        effect_name = available_effects[0];
    }
    std::cout << "[Core] Selected fallback visual effect: '" << effect_name << "'" << std::endl;

    wallpaper.set_plugin_manager(&plugin_manager, effect_name);
    
    // 10. Connect to the Wayland display and bind compositor interfaces (layer-shell, EGL)
    if (!wallpaper.initialize()) {
        std::cerr << "\033[31mCRITICAL: Failed to connect to the Wayland compositor or initialize EGL.\033[0m" << std::endl;
        return 1;
    }

    // 11. Initialize and start all active Data Providers based on Lua settings
    plugin_manager.initialize_providers(&wallpaper, [&lua_engine](IDataProviderABI* p) {
        return lua_engine.configure_provider(p);
    });

    // 12. Enter the zero-latency epoll event loop
    std::cout << "\033[32m[Core] Entering main epoll event loop...\033[0m" << std::endl;
    wallpaper.run();
    
    std::cout << "[Core] Microkernel shut down gracefully. Clean RAII resource deallocation in progress..." << std::endl;
    return 0;
}