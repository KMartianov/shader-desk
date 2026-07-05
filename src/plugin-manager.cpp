// src/plugin-manager.cpp
#include "plugin-manager.hpp"
#include <dlfcn.h>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

// Internal structure for visual plugin metadata
struct PluginManager::PluginHandle {
    void* handle = nullptr;
    // [NEW] Using C-ABI interface for safe creation/destruction
    IWallpaperEffectABI* (*create_func)() = nullptr;
    void (*destroy_func)(IWallpaperEffectABI*) = nullptr;
    std::string name;
    std::string path;

    ~PluginHandle() {
        if (handle) {
            dlclose(handle);
        }
    }
};

PluginManager::PluginManager(const std::string& plugin_dir) : plugin_directory(plugin_dir) {}

PluginManager::~PluginManager() {
    // IMPORTANT: Correct destruction order.
    // First, destroy the objects themselves (calling destroy_provider from .so)
    data_providers.clear();
    
    // Only then unload the libraries from system memory
    for (void* handle : provider_handles) {
        if (handle) dlclose(handle);
    }
    provider_handles.clear();
    
    // loaded_plugins will destroy themselves (triggering dlclose in ~PluginHandle)
}

void PluginManager::discover_plugins() {
    std::cout << "Scanning for plugin bundles in: " << plugin_directory << std::endl;
    data_providers.clear();
    for (void* handle : provider_handles) if (handle) dlclose(handle);
    provider_handles.clear();
    loaded_plugins.clear();
    bundle_paths_.clear(); // [NEW] Очищаем пути

    try {
        if (!fs::exists(plugin_directory)) return;

        // [IMPORTANT] Опция follow_directory_symlink критична для разработки через симлинки!
        auto options = fs::directory_options::follow_directory_symlink | fs::directory_options::skip_permission_denied;

        for (const auto& entry : fs::recursive_directory_iterator(plugin_directory, options)) {

            std::string path_str = entry.path().string();
            if (path_str.find("/build/") != std::string::npos || path_str.find("/.git/") != std::string::npos) {
                continue;
            }


            if (entry.is_regular_file() && entry.path().extension() == ".so") {
                const std::string path_str = entry.path().string();
                const std::string bundle_dir = entry.path().parent_path().string();
                
                void* handle = dlopen(path_str.c_str(), RTLD_LAZY);
                if (!handle) {
                    std::cerr << "Failed to load " << path_str << ": " << dlerror() << std::endl;
                    continue;
                }

                bool is_valid_plugin = false;
                bool handle_stored = false;

                // 1. Visual Effect
                auto create_effect_fn = (IWallpaperEffectABI* (*)())dlsym(handle, "create_effect");
                auto destroy_effect_fn = (void (*)(IWallpaperEffectABI*))dlsym(handle, "destroy_effect");
                
                if (create_effect_fn && destroy_effect_fn) {
                    IWallpaperEffectABI* temp = create_effect_fn();
                    std::string effect_name = temp->get_name();
                    destroy_effect_fn(temp);

                    auto plugin = std::make_unique<PluginHandle>();
                    plugin->handle = handle;
                    plugin->create_func = create_effect_fn;
                    plugin->destroy_func = destroy_effect_fn;
                    plugin->name = effect_name;
                    plugin->path = path_str;
                    
                    // Запоминаем путь к бандлу для этого эффекта!
                    bundle_paths_[effect_name] = bundle_dir;

                    loaded_plugins.push_back(std::move(plugin));
                    std::cout << "  - Discovered Bundle: '" << effect_name << "' in " << bundle_dir << std::endl;
                    is_valid_plugin = true;
                    handle_stored = true;
                }

                // 2. Data Provider (аналогично)
                auto create_provider_fn = (IDataProviderABI* (*)())dlsym(handle, "create_provider");
                auto destroy_provider_fn = (void (*)(IDataProviderABI*))dlsym(handle, "destroy_provider");
                if (create_provider_fn && destroy_provider_fn) {
                    std::unique_ptr<IDataProviderABI, void(*)(IDataProviderABI*)> provider(create_provider_fn(), destroy_provider_fn);
                    bundle_paths_[provider->get_name()] = bundle_dir;
                    data_providers.push_back(std::move(provider));
                    if (!handle_stored) provider_handles.push_back(handle);
                    is_valid_plugin = true;
                    std::cout << "  - Discovered Provider Bundle: '" << data_providers.back()->get_name() << "'" << std::endl;
                }

                if (!is_valid_plugin) dlclose(handle);
            }
        }
    } catch (const fs::filesystem_error& e) {
        std::cerr << "FS Error during plugin scanning: " << e.what() << std::endl;
    }
}

std::string PluginManager::get_bundle_path(const std::string& effect_name) const {
    auto it = bundle_paths_.find(effect_name);
    return (it != bundle_paths_.end()) ? it->second : "";
}

void PluginManager::initialize_providers(ICoreContextABI* core, const std::function<bool(IDataProviderABI*)>& configure_callback) {
    for (auto& provider : data_providers) {
        bool enabled = true;
        
        // Read Lua settings (returns true if enabled = true)
        if (configure_callback) {
            enabled = configure_callback(provider.get());
        }

        if (enabled) {
            // If already initialized, provider->initialize simply returns true
            if (!provider->initialize(core)) {
                std::cerr << "Failed to initialize Data Provider: " << provider->get_name() << std::endl;
            } else {
                std::cout << "Data Provider running: " << provider->get_name() << std::endl;
            }
        } else {
            // NEW: If plugin was disabled in config on the fly - SHUT IT DOWN!
            // cleanup() safely unregisters it from epoll and closes the socket.
            provider->cleanup();
            std::cout << "Data Provider stopped (disabled in config): " << provider->get_name() << std::endl;
        }
    }
}



WallpaperEffectPtr PluginManager::create_effect(const std::string& effect_name) {
    for (const auto& plugin : loaded_plugins) {
        if (plugin->name == effect_name) {
            // [NEW] Get raw ABI pointer
            IWallpaperEffectABI* raw_ptr = plugin->create_func();
            if (raw_ptr) {
                // Return smart pointer with correct custom deleter from .so
                return WallpaperEffectPtr(raw_ptr, plugin->destroy_func);
            }
        }
    }
    std::cerr << "Error: Plugin with name '" << effect_name << "' not found." << std::endl;
    return WallpaperEffectPtr(nullptr, nullptr);
}

std::vector<std::string> PluginManager::get_available_effects() const {
    std::vector<std::string> names;
    for (const auto& plugin : loaded_plugins) {
        names.push_back(plugin->name);
    }
    return names;
}