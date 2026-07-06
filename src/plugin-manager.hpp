#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional> // Added for std::function

#include <unordered_map>
#include <nlohmann/json.hpp>


#include "plugin-abi.hpp"       // [NEW] Include ABI interfaces
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
     * @brief Инициализирует найденные Data Providers.
     * @param core Контекст ядра (epoll, BlackBoard). Теперь использует ABI интерфейс.
     * @param configure_callback Коллбэк для конфигурации. Должен прочитать настройки из Lua,
     *                           передать их через provider->set_parameter() и вернуть true.
     *                           Если в Lua стоит enabled=false, должен вернуть false.
     *                           [NEW] Теперь принимает IDataProviderABI*.
     */
    void initialize_providers(ICoreContextABI* core, const std::function<bool(IDataProviderABI*)>& configure_callback = nullptr);

    // Visual effect factory (creates new instance by name)
    WallpaperEffectPtr create_effect(const std::string& effect_name);
    
    // Returns list of available visual effect names
    std::vector<std::string> get_available_effects() const;

private:
    struct PluginHandle;
    
    // Storage for visual effect factories
    std::vector<std::unique_ptr<PluginHandle>> loaded_plugins;
    
    // Storage for ACTIVE data providers (with custom deleter from .so)
    // [NEW] Store pointers to ABI interfaces to guarantee binary compatibility
    std::vector<std::unique_ptr<IDataProviderABI, void(*)(IDataProviderABI*)>> data_providers;
    
    // Storage for dlopen handles of Data Providers.
    std::vector<void*> provider_handles;

    std::vector<std::string> search_directories;
    std::unordered_map<std::string, std::string> bundle_paths_;
};