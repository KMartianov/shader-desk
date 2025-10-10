// src/plugin-interrogator.cpp
#include "wallpaper-effect.hpp"
#include <nlohmann/json.hpp>
#include <iostream>
#include <string>
#include <dlfcn.h>
#include <memory>

using json = nlohmann::json;

// Helper для конвертации std::variant в nlohmann::json
void variant_to_json(json& j, const std::string& key, const EffectParameterValue& value) {
    std::visit([&](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, glm::vec3>) {
            j[key] = {arg.x, arg.y, arg.z};
        } else {
            j[key] = arg;
        }
    }, value);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <path_to_plugin.so>" << std::endl;
        return 1;
    }

    const char* plugin_path = argv[1];

    void* handle = dlopen(plugin_path, RTLD_LAZY);
    if (!handle) {
        std::cerr << "Failed to load plugin " << plugin_path << ": " << dlerror() << std::endl;
        return 1;
    }

    auto create_func = (WallpaperEffect* (*)())dlsym(handle, "create_effect");
    auto destroy_func = (void (*)(WallpaperEffect*))dlsym(handle, "destroy_effect");

    if (!create_func || !destroy_func) {
        std::cerr << "Plugin " << plugin_path << " is missing required symbols." << std::endl;
        dlclose(handle);
        return 1;
    }

    std::unique_ptr<WallpaperEffect, decltype(destroy_func)> effect(create_func(), destroy_func);

    if (!effect) {
        std::cerr << "create_effect() failed for plugin: " << plugin_path << std::endl;
        dlclose(handle);
        return 1;
    }
    
    // Получаем и параметры, и каноническое имя
    std::vector<EffectParameter> params = effect->get_parameters();
    std::string canonical_name = effect->get_name();
    
    // Конвертируем параметры в JSON
    json params_json = json::object();
    for (const auto& param : params) {
        variant_to_json(params_json, param.name, param.value);
    }

    // Создаем корневой JSON-объект с именем и параметрами
    json result_json = {
        {"name", canonical_name},
        {"defaults", params_json}
    };
    
    // Выводим результат в stdout
    std::cout << result_json.dump(2) << std::endl;

    dlclose(handle);
    return 0;
}