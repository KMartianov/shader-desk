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

                auto get_version_fn = (uint32_t (*)())dlsym(handle, "get_abi_version");
                if (get_version_fn) {
                    uint32_t plugin_version = get_version_fn();
                    if (plugin_version != SHADER_DESK_ABI_VERSION) {
                        std::cerr << "\033[31m[PluginManager] Rejected '" << path_str 
                                  << "': ABI mismatch (Plugin v" << plugin_version 
                                  << " vs Core v" << SHADER_DESK_ABI_VERSION << ").\033[0m" << std::endl;
                        dlclose(handle);
                        continue;
                    }
                } else {
                    std::cerr << "[PluginManager] Warning: '" << path_str << "' has no get_abi_version(). Assuming v1." << std::endl;
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

std::vector<std::string> PluginManager::get_available_providers() const {
    std::vector<std::string> names;
    for (const auto& provider : data_providers) {
        names.push_back(provider->get_name());
    }
    return names;
}

nlohmann::json PluginManager::inspect_plugin(const std::string& plugin_name) {
    nlohmann::json root = nlohmann::json::object();
    
    // Вспомогательная лямбда для сериализации параметров ABI в JSON
    auto serialize_params = [](uint32_t count, auto* plugin_ptr) -> nlohmann::json {
        nlohmann::json j_params = nlohmann::json::array();
        for (uint32_t i = 0; i < count; ++i) {
            ParamInfoABI info;
            plugin_ptr->get_parameter_info(i, &info);
            
            nlohmann::json p;
            p["name"] = info.name;
            p["description"] = info.description;
            
            switch (info.default_value.type) {
                case ParamType::TYPE_BOOL:   p["type"] = "bool"; p["default"] = info.default_value.b_val; break;
                case ParamType::TYPE_INT:    p["type"] = "int"; p["default"] = info.default_value.i_val; break;
                case ParamType::TYPE_FLOAT:  p["type"] = "float"; p["default"] = info.default_value.f_val; break;
                case ParamType::TYPE_STRING: p["type"] = "string"; p["default"] = info.default_value.s_val; break;
                case ParamType::TYPE_VEC3:   
                    p["type"] = "vec3"; 
                    p["default"] = {info.default_value.vec3_val[0], info.default_value.vec3_val[1], info.default_value.vec3_val[2]}; 
                    break;
            }
            j_params.push_back(p);
        }
        return j_params;
    };

    // 1. Ищем среди эффектов
    for (const auto& plugin : loaded_plugins) {
        if (plugin->name == plugin_name) {
            WallpaperEffectPtr effect = create_effect(plugin_name);
            if (effect) {
                root["name"] = effect->get_name();
                root["type"] = "effect";
                root["bundle_path"] = get_bundle_path(plugin_name);
                root["parameters"] = serialize_params(effect->get_parameter_count(), effect.get());
                return root;
            }
        }
    }

    // 2. Ищем среди провайдеров данных
    for (const auto& provider : data_providers) {
        if (provider->get_name() == plugin_name) {
            root["name"] = provider->get_name();
            root["type"] = "provider";
            root["bundle_path"] = get_bundle_path(plugin_name);
            root["parameters"] = serialize_params(provider->get_parameter_count(), provider.get());
            return root;
        }
    }

    return nullptr; // Не найден
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