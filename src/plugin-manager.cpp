// src/plugin-manager.cpp
#include "plugin-manager.hpp"
#include <dlfcn.h>
#include <filesystem>
#include <iostream>
#include <unordered_set>

namespace fs = std::filesystem;

// Internal structure for visual plugin metadata and dlopen handles
struct PluginManager::PluginHandle {
    void* handle = nullptr;
    // Using C-ABI interface for safe creation/destruction across the library boundary
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

PluginManager::PluginManager(const std::vector<std::string>& dirs) : search_directories(dirs) {}

PluginManager::~PluginManager() {
    // IMPORTANT: Correct destruction order is critical to prevent segfaults.
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
    // 1. Clear previous state (crucial for safe hot-reloading of the plugin list)
    data_providers.clear();
    for (void* handle : provider_handles) {
        if (handle) dlclose(handle);
    }
    provider_handles.clear();
    loaded_plugins.clear();
    bundle_paths_.clear();

    auto options = fs::directory_options::follow_directory_symlink | fs::directory_options::skip_permission_denied;

    // Sets to track uniqueness by plugin name.
    // If a plugin with a specific name is already loaded (e.g., from ~/.config/), 
    // the system version from /usr/lib/ will be gracefully ignored.
    std::unordered_set<std::string> discovered_effects;
    std::unordered_set<std::string> discovered_providers;

    // 2. Iterate through directory list in priority order (User -> System -> Local)
    for (const auto& dir : search_directories) {
        if (!fs::exists(dir)) continue;
        
        std::cout << "[PluginManager] Scanning for plugin bundles in: " << dir << std::endl;

        try {
            for (const auto& entry : fs::recursive_directory_iterator(dir, options)) {
                std::string path_str = entry.path().string();
                
                // Ignore build directories and git cache
                if (path_str.find("/build/") != std::string::npos || 
                    path_str.find("/.git/") != std::string::npos) {
                    continue;
                }

                if (entry.is_regular_file() && entry.path().extension() == ".so") {
                    const std::string bundle_dir = entry.path().parent_path().string();
                    
                    // Load the shared library into memory
                    void* handle = dlopen(path_str.c_str(), RTLD_LAZY);
                    if (!handle) {
                        // Silent skip (e.g., user is currently recompiling their mod)
                        continue; 
                    }

                    // --- ABI COMPATIBILITY CHECK ---
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
                        std::cerr << "[PluginManager] Warning: '" << path_str 
                                  << "' has no get_abi_version(). Assuming v1." << std::endl;
                    }

                    bool handle_stored = false;

                    // --- 3. CHECK & LOAD VISUAL EFFECT ---
                    auto create_effect_fn = (IWallpaperEffectABI* (*)())dlsym(handle, "create_effect");
                    auto destroy_effect_fn = (void (*)(IWallpaperEffectABI*))dlsym(handle, "destroy_effect");
                    
                    if (create_effect_fn && destroy_effect_fn) {
                        // Create a temporary instance to read its name
                        IWallpaperEffectABI* temp = create_effect_fn();
                        std::string effect_name = temp->get_name();
                        destroy_effect_fn(temp);

                        // Check if this plugin was already overridden by the user workspace
                        if (discovered_effects.find(effect_name) == discovered_effects.end()) {
                            auto plugin = std::make_unique<PluginHandle>();
                            plugin->handle = handle;
                            plugin->create_func = create_effect_fn;
                            plugin->destroy_func = destroy_effect_fn;
                            plugin->name = effect_name;
                            plugin->path = path_str;
                            
                            std::string final_path = bundle_dir;
                            if (!fs::exists(fs::path(bundle_dir) / "shaders")) {
                                #ifdef LOCAL_SOURCE_DIR
                                if (fs::exists(fs::path(LOCAL_SOURCE_DIR) / effect_name)) {
                                    final_path = (fs::path(LOCAL_SOURCE_DIR) / effect_name).string();
                                }
                                #endif
                            }
                            bundle_paths_[effect_name] = final_path;

                            
                            loaded_plugins.push_back(std::move(plugin));
                            discovered_effects.insert(effect_name);
                            handle_stored = true;
                            
                            std::cout << "  - Discovered Bundle: '" << effect_name << "' in " << bundle_dir << std::endl;
                        } else {
                            std::cout << "  - Skipped System Bundle: '" << effect_name 
                                      << "' (Overridden by user workspace)" << std::endl;
                        }
                    }

                    // --- 4. CHECK & LOAD DATA PROVIDER ---
                    auto create_provider_fn = (IDataProviderABI* (*)())dlsym(handle, "create_provider");
                    auto destroy_provider_fn = (void (*)(IDataProviderABI*))dlsym(handle, "destroy_provider");
                    
                    if (create_provider_fn && destroy_provider_fn) {
                        // Instantiate the provider immediately (wrap in smart pointer with custom .so deleter)
                        std::unique_ptr<IDataProviderABI, void(*)(IDataProviderABI*)> provider(create_provider_fn(), destroy_provider_fn);
                        std::string provider_name = provider->get_name();

                        if (discovered_providers.find(provider_name) == discovered_providers.end()) {
                            
                            std::string final_prov_path = bundle_dir;
                            if (!fs::exists(fs::path(bundle_dir) / "shaders") && !fs::exists(fs::path(bundle_dir) / "presets")) {
                                #ifdef LOCAL_SOURCE_DIR
                                if (fs::exists(fs::path(LOCAL_SOURCE_DIR) / provider_name)) {
                                    final_prov_path = (fs::path(LOCAL_SOURCE_DIR) / provider_name).string();
                                }
                                #endif
                            }
                            bundle_paths_[provider_name] = final_prov_path;

                            discovered_providers.insert(provider_name);
                            data_providers.push_back(std::move(provider));
                            
                            if (!handle_stored) {
                                provider_handles.push_back(handle);
                                handle_stored = true;
                            }
                            std::cout << "  - Discovered Provider: '" << provider_name << "' in " << bundle_dir << std::endl;
                        } else {
                            std::cout << "  - Skipped System Provider: '" << provider_name 
                                      << "' (Overridden by user workspace)" << std::endl;
                        }
                    }

                    // 5. Memory Leak Protection:
                    // If the library lacks required exports OR both plugins were ignored 
                    // (due to user overrides), unload the .so from RAM.
                    if (!handle_stored) {
                        dlclose(handle);
                    }
                }
            }
        } catch (const fs::filesystem_error& e) {
            std::cerr << "[PluginManager] FS Error during scanning '" << dir << "': " << e.what() << std::endl;
        }
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
    
    // Helper lambda to serialize ABI parameters into JSON
    auto serialize_params = [](uint32_t count, auto* plugin_ptr) -> nlohmann::json {
        nlohmann::json j_params = nlohmann::json::array();
        for (uint32_t i = 0; i < count; ++i) {
            ParamInfoABI info;
            plugin_ptr->get_parameter_info_abi(i, &info);
            
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

    // 1. Search among visual effects
    for (const auto& plugin : loaded_plugins) {
        if (plugin->name == plugin_name) {
            WallpaperEffectPtr effect = create_effect(plugin_name);
            if (effect) {
                root["name"] = effect->get_name();
                root["type"] = "effect";
                root["bundle_path"] = get_bundle_path(plugin_name);
                root["parameters"] = serialize_params(effect->get_parameter_count_abi(), effect.get());
                return root;
            }
        }
    }

    // 2. Search among data providers
    for (const auto& provider : data_providers) {
        if (provider->get_name() == plugin_name) {
            root["name"] = provider->get_name();
            root["type"] = "provider";
            root["bundle_path"] = get_bundle_path(plugin_name);
            root["parameters"] = serialize_params(provider->get_parameter_count_abi(), provider.get());
            return root;
        }
    }

    return nullptr; // Not found
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
                std::cerr << "[PluginManager] Failed to initialize Data Provider: " << provider->get_name() << std::endl;
            } else {
                std::cout << "[PluginManager] Data Provider running: " << provider->get_name() << std::endl;
            }
        } else {
            // If plugin was disabled in config on the fly - SHUT IT DOWN!
            // cleanup() safely unregisters it from epoll and closes the socket.
            provider->cleanup();
            std::cout << "[PluginManager] Data Provider stopped (disabled in config): " << provider->get_name() << std::endl;
        }
    }
}

WallpaperEffectPtr PluginManager::create_effect(const std::string& effect_name) {
    for (const auto& plugin : loaded_plugins) {
        if (plugin->name == effect_name) {
            // Get raw ABI pointer
            IWallpaperEffectABI* raw_ptr = plugin->create_func();
            if (raw_ptr) {
                // Return smart pointer with correct custom deleter from .so
                return WallpaperEffectPtr(raw_ptr, plugin->destroy_func);
            }
        }
    }
    std::cerr << "[PluginManager] Error: Plugin with name '" << effect_name << "' not found." << std::endl;
    return WallpaperEffectPtr(nullptr, nullptr);
}

std::vector<std::string> PluginManager::get_available_effects() const {
    std::vector<std::string> names;
    for (const auto& plugin : loaded_plugins) {
        names.push_back(plugin->name);
    }
    return names;
}