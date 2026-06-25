// src/plugin-manager.cpp
#include "plugin-manager.hpp"
#include <dlfcn.h>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

// Внутренняя структура для хранения информации о визуальном плагине
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

PluginManager::~PluginManager() {
    // ВАЖНО: Правильный порядок уничтожения.
    // Сначала удаляем сами объекты (вызывая destroy_provider из .so)
    data_providers.clear();
    
    // А только затем выгружаем библиотеки из памяти системы
    for (void* handle : provider_handles) {
        if (handle) dlclose(handle);
    }
    provider_handles.clear();
    
    // loaded_plugins удалятся сами (и вызовут dlclose в ~PluginHandle)
}

void PluginManager::discover_plugins() {
    std::cout << "Scanning for plugins in: " << plugin_directory << std::endl;
    
    // Очистка перед новым сканированием
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
                
                // Загружаем библиотеку
                void* handle = dlopen(path_str.c_str(), RTLD_LAZY);
                if (!handle) {
                    std::cerr << "Failed to load plugin " << path_str << ": " << dlerror() << std::endl;
                    continue;
                }

                bool is_valid_plugin = false;
                bool handle_stored_by_effect = false; // Флаг для предотвращения двойного dlclose

                // --- 1. Попытка загрузить как Визуальный Эффект (Visual Effect) ---
                auto create_effect_fn = (WallpaperEffect* (*)())dlsym(handle, "create_effect");
                auto destroy_effect_fn = (void (*)(WallpaperEffect*))dlsym(handle, "destroy_effect");
                
                if (create_effect_fn && destroy_effect_fn) {
                    WallpaperEffect* temp_effect = create_effect_fn();
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

                // --- 2. Попытка загрузить как Поставщика Данных (Data Provider) ---
                auto create_provider_fn = (IDataProvider* (*)())dlsym(handle, "create_provider");
                auto destroy_provider_fn = (void (*)(IDataProvider*))dlsym(handle, "destroy_provider");

                if (create_provider_fn && destroy_provider_fn) {
                    // Создаем глобальный инстанс провайдера сразу
                    std::unique_ptr<IDataProvider, void(*)(IDataProvider*)> provider(
                        create_provider_fn(), 
                        destroy_provider_fn
                    );
                    
                    std::cout << "  - Discovered Data Provider: '" << provider->get_name() << "' from " << path_str << std::endl;
                    data_providers.push_back(std::move(provider));
                    
                    // Сохраняем handle, только если он уже не был сохранен визуальным эффектом
                    if (!handle_stored_by_effect) {
                        provider_handles.push_back(handle);
                    }
                    is_valid_plugin = true;
                }

                // --- 3. Если в .so нет нужных сигнатур ---
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

void PluginManager::initialize_providers(ICoreContext* core, const std::function<bool(IDataProvider*)>& configure_callback) {
    for (auto& provider : data_providers) {
        bool enabled = true;
        
        // Если передан коллбэк, отдаем ему Провайдер на настройку.
        // Коллбэк вызовет set_parameter() и вернет флаг включения.
        if (configure_callback) {
            enabled = configure_callback(provider.get());
        }

        if (enabled) {
            if (!provider->initialize(core)) {
                std::cerr << "Failed to initialize Data Provider: " << provider->get_name() << std::endl;
            } else {
                std::cout << "Data Provider started: " << provider->get_name() << std::endl;
            }
        } else {
            std::cout << "Data Provider skipped (disabled in config): " << provider->get_name() << std::endl;
        }
    }
}

WallpaperEffectPtr PluginManager::create_effect(const std::string& effect_name) {
    for (const auto& plugin : loaded_plugins) {
        if (plugin->name == effect_name) {
            WallpaperEffect* raw_ptr = plugin->create_func();
            if (raw_ptr) {
                // Возвращаем smart pointer с правильным кастомным deleter'ом из .so
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