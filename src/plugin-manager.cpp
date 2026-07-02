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
    std::cout << "Scanning for plugins in: " << plugin_directory << std::endl;
    
    // Cleanup before rescanning
    data_providers.clear();
    for (void* handle : provider_handles) {
        if (handle) dlclose(handle);
    }
    provider_handles.clear();
    loaded_plugins.clear();

    try {
        if (!fs::exists(plugin_directory) || !fs::is_directory(plugin_directory)) {
            std::cerr << "Plugin directory does not exist: " << plugin_directory << std::endl;
            return;
        }

        for (const auto& entry : fs::directory_iterator(plugin_directory)) {
            if (entry.path().extension() == ".so") {
                const std::string path_str = entry.path().string();
                
                // Load shared library
                void* handle = dlopen(path_str.c_str(), RTLD_LAZY);
                if (!handle) {
                    std::cerr << "Failed to load plugin " << path_str << ": " << dlerror() << std::endl;
                    continue;
                }

                bool is_valid_plugin = false;
                bool handle_stored_by_effect = false; // Flag to prevent double dlclose

                // --- 1. Attempt to load as Visual Effect ---
                // [NEW] Expecting IWallpaperEffectABI* return instead of WallpaperEffect*
                auto create_effect_fn = (IWallpaperEffectABI* (*)())dlsym(handle, "create_effect");
                auto destroy_effect_fn = (void (*)(IWallpaperEffectABI*))dlsym(handle, "destroy_effect");
                
                if (create_effect_fn && destroy_effect_fn) {
                    IWallpaperEffectABI* temp_effect = create_effect_fn();
                    std::string effect_name = temp_effect->get_name();
                    destroy_effect_fn(temp_effect);

                    auto plugin = std::make_unique<PluginHandle>();
                    plugin->handle = handle;
                    plugin->create_func = create_effect_fn;
                    plugin->destroy_func = destroy_effect_fn;
                    plugin->name = effect_name;
                    plugin->path = path_str;
                    
                    loaded_plugins.push_back(std::move(plugin));
                    std::cout << "  - Discovered Visual Effect: '" << effect_name << "' from " << path_str << std::endl;
                    
                    is_valid_plugin = true;
                    handle_stored_by_effect = true; 
                }

                // --- 2. Attempt to load as Data Provider ---
                // [NEW] Expecting IDataProviderABI* return instead of IDataProvider*
                auto create_provider_fn = (IDataProviderABI* (*)())dlsym(handle, "create_provider");
                auto destroy_provider_fn = (void (*)(IDataProviderABI*))dlsym(handle, "destroy_provider");

                if (create_provider_fn && destroy_provider_fn) {
                    // Create global provider instance immediately (with custom ABI deleter)
                    std::unique_ptr<IDataProviderABI, void(*)(IDataProviderABI*)> provider(
                        create_provider_fn(), 
                        destroy_provider_fn
                    );
                    
                    std::cout << "  - Discovered Data Provider: '" << provider->get_name() << "' from " << path_str << std::endl;
                    data_providers.push_back(std::move(provider));
                    
                    // Store handle only if it wasn't already stored by the visual effect
                    if (!handle_stored_by_effect) {
                        provider_handles.push_back(handle);
                    }
                    is_valid_plugin = true;
                }

                // --- 3. If .so lacks required signatures ---
                if (!is_valid_plugin) {
                    std::cerr << "Plugin " << path_str << " is missing required symbols. Ignored." << std::endl;
                    dlclose(handle);
                }
            }
        }
    } catch (const fs::filesystem_error& e) {
        std::cerr << "Filesystem error while scanning for plugins: " << e.what() << std::endl;
    }
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