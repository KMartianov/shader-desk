// main.cpp
#include "interactive-wallpaper.hpp"
#include "plugin-manager.hpp" // Новый менеджер плагинов
#include "config-loader.hpp"
#include <iostream>
#include <memory>
#include <string>
#include <nlohmann/json.hpp>

using json = nlohmann::json;


std::string get_plugin_directory() {
    // В реальном приложении путь лучше делать более надежным
    return std::string(getenv("HOME")) + "/.config/interactive-wallpaper/effects";
}

int main(int argc, char** argv) {
    // 1. Загрузка основной конфигурации
    json config = load_config();
    
    // 2. Инициализация менеджера плагинов
    PluginManager plugin_manager(get_plugin_directory());
    plugin_manager.discover_plugins();

    auto available_effects = plugin_manager.get_available_effects();
    if (available_effects.empty()) {
        std::cerr << "No plugins found in " << get_plugin_directory() << ". Exiting." << std::endl;
        return 1;
    }

    // 3. Определение, какой эффект использовать
    std::string effect_name = config.value("effect_name", available_effects[0]); // По умолчанию первый найденный
    
    std::cout << "Attempting to load effect: '" << effect_name << "'" << std::endl;

    // 4. Создание экземпляра эффекта через менеджер
    WallpaperEffectPtr effect = plugin_manager.create_effect(effect_name);
    
    if (!effect) {
        std::cerr << "Failed to create an instance of effect '" << effect_name << "'. Exiting." << std::endl;
        return 1;
    }
    
    std::cout << "Successfully created effect instance." << std::endl;

    // 5. Применение настроек из конфигурации к эффекту
    if (config.contains("effect_settings")) {
        std::cout << "Applying settings from config file..." << std::endl;
        for (const auto& item : config["effect_settings"].items()) {
            std::cout << "  - Setting '" << item.key() << "'..." << std::endl;
            effect->set_parameter(item.key(), json_to_variant(item.value()));
        }
    }

    // 6. Настройка и запуск приложения обоев
    WallpaperConfig wallpaper_config;
    wallpaper_config.output_name = "*";
    wallpaper_config.interactive = config.value("interactive", true);

    InteractiveWallpaper wallpaper(wallpaper_config);
    
    if (!wallpaper.initialize()) {
        std::cerr << "Failed to initialize wallpaper" << std::endl;
        return 1;
    }

    wallpaper.set_effect("*", std::move(effect));
    
    std::cout << "Starting wallpaper main loop..." << std::endl;
    wallpaper.run();
    
    return 0;
}