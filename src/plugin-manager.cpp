// plugin-manager.cpp
#include "plugin-manager.hpp"
#include <dlfcn.h>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

struct PluginManager::PluginHandle {
    void* handle = nullptr;
    WallpaperEffect* (*create_func)() = nullptr;
    void (*destroy_func)(WallpaperEffect*) = nullptr;
    std::string name;
    std::string path;

    ~PluginHandle() {
        if (handle) {
            dlclose(handle);
        }
    }
};

PluginManager::PluginManager(const std::string& plugin_dir) : plugin_directory(plugin_dir) {}

PluginManager::~PluginManager() = default;

void PluginManager::discover_plugins() {
    std::cout << "Scanning for plugins in: " << plugin_directory << std::endl;
    loaded_plugins.clear();

    try {
        if (!fs::exists(plugin_directory) || !fs::is_directory(plugin_directory)) {
            std::cerr << "Plugin directory does not exist: " << plugin_directory << std::endl;
            return;
        }

        for (const auto& entry : fs::directory_iterator(plugin_directory)) {
            if (entry.path().extension() == ".so") {
                const std::string path_str = entry.path().string();
                void* handle = dlopen(path_str.c_str(), RTLD_LAZY);
                if (!handle) {
                    std::cerr << "Failed to load plugin " << path_str << ": " << dlerror() << std::endl;
                    continue;
                }

                auto create_func = (WallpaperEffect* (*)())dlsym(handle, "create_effect");
                auto destroy_func = (void (*)(WallpaperEffect*))dlsym(handle, "destroy_effect");
                
                if (!create_func || !destroy_func) {
                    std::cerr << "Plugin " << path_str << " is missing required symbols 'create_effect' or 'destroy_effect'." << std::endl;
                    dlclose(handle);
                    continue;
                }

                WallpaperEffect* temp_effect = create_func();
                std::string effect_name = temp_effect->get_name();
                destroy_func(temp_effect);

                auto plugin = std::make_unique<PluginHandle>();
                plugin->handle = handle;
                plugin->create_func = create_func;
                plugin->destroy_func = destroy_func;
                plugin->name = effect_name;
                plugin->path = path_str;
                
                loaded_plugins.push_back(std::move(plugin));
                std::cout << "  - Discovered plugin: '" << effect_name << "' from " << path_str << std::endl;
            }
        }
    } catch (const fs::filesystem_error& e) {
        std::cerr << "Filesystem error while scanning for plugins: " << e.what() << std::endl;
    }
}

WallpaperEffectPtr PluginManager::create_effect(const std::string& effect_name) {
    for (const auto& plugin : loaded_plugins) {
        if (plugin->name == effect_name) {
            WallpaperEffect* raw_ptr = plugin->create_func();
            if (raw_ptr) {
                // Возвращаем правильный тип с кастомным deleter'ом
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