#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional> // Added for std::function

#include <unordered_map>
#include <nlohmann/json.hpp>


#include "plugin-abi.hpp"       // Include ABI interfaces
#include "wallpaper-effect.hpp" // Include for WallpaperEffectPtr
#include "data-provider.hpp"    


class PluginManager {
public:
    PluginManager(const std::vector<std::string>& plugin_dirs);
    ~PluginManager();

    // Scans directory for .so files and loads them
    void discover_plugins();
    std::string get_bundle_path(const std::string& effect_name) const;

    std::vector<std::string> get_available_providers() const;
    nlohmann::json inspect_plugin(const std::string& plugin_name);
    
    /**
     * @brief Initializes discovered Data Providers.
     * @param core Core context (epoll, BlackBoard). Uses the ABI interface.
     * @param configure_callback Configuration callback. Must read settings from Lua,
     *                           pass them via provider->set_parameter() and return true.
     *                           If enabled=false is set in Lua, it must return false.
     *                           Accepts IDataProviderABI*.
     */
    void initialize_providers(ICoreContextABI* core, const std::function<bool(IDataProviderABI*)>& configure_callback = nullptr);

    // Visual effect factory (creates new instance by name)
    WallpaperEffectPtr create_effect(const std::string& effect_name);
    
    // Returns list of available visual effect names
    std::vector<std::string> get_available_effects() const;

private:
    struct PluginHandle;
    
    // Storage for visual effect factories
    std::vector<std::shared_ptr<PluginHandle>> loaded_plugins;
    
    // Storage for ACTIVE data providers (with custom deleter from .so)
    // Store pointers to ABI interfaces to guarantee binary compatibility
    std::vector<std::unique_ptr<IDataProviderABI, void(*)(IDataProviderABI*)>> data_providers;
    
    // Storage for dlopen handles of Data Providers.
    std::vector<void*> provider_handles;

    std::vector<std::string> search_directories;
    std::unordered_map<std::string, std::string> bundle_paths_;
};