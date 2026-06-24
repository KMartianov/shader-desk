// src/main.cpp
#include "interactive-wallpaper.hpp"
#include "plugin-manager.hpp"
#include "lua-engine.hpp"
#include "lua-config-generator.hpp"
#include <iostream>
#include <string>
#include <atomic>
#include <signal.h>
#include <sys/signalfd.h>

std::atomic<bool> global_running{true};

std::string get_plugin_directory() {
    const char* home = getenv("HOME");
    return home ? std::string(home) + "/.config/interactive-wallpaper/effects" : "./effects";
}

int main(int argc, char** argv) {
    // 1. Обработка CLI команд (Генерация конфигов)
    if (argc > 1 && std::string(argv[1]) == "--init-config") {
        PluginManager pm(get_plugin_directory());
        pm.discover_plugins();
        LuaConfigGenerator::generate_configs(pm);
        return 0; // Выходим, Wayland нам тут не нужен
    }

    // 2. Блокируем сигналы (Они будут обрабатываться через signalfd в epoll)
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    if (sigprocmask(SIG_BLOCK, &mask, nullptr) == -1) {
        std::cerr << "Failed to block signals" << std::endl;
        return 1;
    }

    // 3. Загрузка конфигурации через Lua
    LuaEngine lua_engine;
    if (!lua_engine.load()) {
        std::cerr << "Failed to load Lua configuration!" << std::endl;
        // Не выходим, попытаемся запуститься с дефолтными значениями
    }

    // 4. Инициализация менеджера плагинов
    PluginManager plugin_manager(get_plugin_directory());
    plugin_manager.discover_plugins();

    auto available_effects = plugin_manager.get_available_effects();
    if (available_effects.empty()) {
        std::cerr << "No visual plugins found! Exiting." << std::endl;
        return 1;
    }

    // Выбираем эффект: из Lua конфига или первый найденный
    std::string effect_name = lua_engine.get_active_effect();
    if (effect_name.empty() || std::find(available_effects.begin(), available_effects.end(), effect_name) == available_effects.end()) {
        effect_name = available_effects[0];
    }
    std::cout << "Selected effect: '" << effect_name << "'" << std::endl;

    // 5. Настройка базовых параметров
    WallpaperConfig wallpaper_config;
    wallpaper_config.output_name = "*";
    wallpaper_config.interactive = lua_engine.is_interactive();

    InteractiveWallpaper wallpaper(wallpaper_config, lua_engine);
    wallpaper.set_plugin_manager(&plugin_manager, effect_name);
    
    if (!wallpaper.initialize()) {
        std::cerr << "Failed to initialize Wayland compositor connection" << std::endl;
        return 1;
    }
    plugin_manager.initialize_providers(&wallpaper);

    std::cout << "Starting wallpaper main loop..." << std::endl;
    wallpaper.run();
    
    return 0;
}