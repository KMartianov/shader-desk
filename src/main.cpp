// src/main.cpp
#include "interactive-wallpaper.hpp"
#include "plugin-manager.hpp"
#include "config-loader.hpp"
#include <iostream>
#include <string>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

std::string get_plugin_directory() {
    // Получаем домашнюю директорию для поиска плагинов
    const char* home = getenv("HOME");
    if (home) {
        return std::string(home) + "/.config/interactive-wallpaper/effects";
    }
    return "./effects"; // Fallback
}

int main(int argc, char** argv) {
    // 1. Загрузка глобальной конфигурации
    json config = load_config();
    
    // 2. Инициализация менеджера плагинов и поиск .so файлов
    PluginManager plugin_manager(get_plugin_directory());
    plugin_manager.discover_plugins();

    auto available_effects = plugin_manager.get_available_effects();
    if (available_effects.empty()) {
        std::cerr << "No plugins found in " << get_plugin_directory() << ". Exiting." << std::endl;
        return 1;
    }

    // 3. Выбор эффекта (из конфига или первого доступного)
    std::string effect_name = config.value("effect_name", available_effects[0]);
    std::cout << "Selected effect: '" << effect_name << "'" << std::endl;

    // 4. Настройка базовых параметров приложения
    WallpaperConfig wallpaper_config;
    wallpaper_config.output_name = "*"; // Применять ко всем найденным мониторам
    wallpaper_config.interactive = config.value("interactive", true);

    InteractiveWallpaper wallpaper(wallpaper_config);
    
    // 5. ПЕРЕДАЕМ ФАБРИКУ ПЛАГИНОВ В ЯДРО
    // Теперь Wayland-клиент сам создаст экземпляр эффекта для каждого монитора (Output)
    wallpaper.set_plugin_manager(&plugin_manager, effect_name);
    
    // 6. Подключение к Wayland, инициализация EGL и IPC-сокетов
    if (!wallpaper.initialize()) {
        std::cerr << "Failed to initialize wallpaper" << std::endl;
        return 1;
    }

    // 7. Запуск главного цикла событий (zero-latency epoll loop)
    std::cout << "Starting wallpaper main loop..." << std::endl;
    wallpaper.run();
    
    return 0;
}