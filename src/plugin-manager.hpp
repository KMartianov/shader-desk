// src/plugin-manager.hpp

#pragma once

#include <string>
#include <vector>
#include <memory>
#include "wallpaper-effect.hpp" // <-- ADD THIS INCLUDE

class PluginManager {
public:
    PluginManager(const std::string& plugin_dir);
    ~PluginManager();

    void discover_plugins();
    WallpaperEffectPtr create_effect(const std::string& effect_name);
    std::vector<std::string> get_available_effects() const;

private:
    struct PluginHandle;
    std::vector<std::unique_ptr<PluginHandle>> loaded_plugins;
    std::string plugin_directory;
};