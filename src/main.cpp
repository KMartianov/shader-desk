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
#include <filesystem>
#include <algorithm>

std::atomic<bool> global_running{true};

std::string get_plugin_directory() {
    const char* home = getenv("HOME");
    return home ? std::string(home) + "/.config/interactive-wallpaper/effects" : "./effects";
}

int main(int argc, char** argv) {
    // 1. Обработка CLI команд (Генерация конфигов и копирование пресетов)
    if (argc > 1 && std::string(argv[1]) == "--init-config") {
        std::string plugin_dir = get_plugin_directory();
        PluginManager pm(plugin_dir);
        pm.discover_plugins();
        LuaConfigGenerator::generate_configs(pm);
        
        // Автоматическое копирование пресетов из папок плагинов в ~/.config/...
        try {
            namespace fs = std::filesystem;
            fs::path target_presets = std::string(getenv("HOME")) + "/.config/interactive-wallpaper/presets";
            fs::create_directories(target_presets);
            
            // Ищем все папки "presets" в директории установки плагинов
            for (const auto& entry : fs::recursive_directory_iterator(plugin_dir)) {
                if (entry.is_directory() && entry.path().filename() == "presets") {
                    // Имя родительской папки (имя плагина, например ico-sphere-effect)
                    std::string plugin_folder_name = entry.path().parent_path().filename().string();
                    // Чтобы sanitize_plugin_name (с нижними подчеркиваниями) работал правильно:
                    std::replace(plugin_folder_name.begin(), plugin_folder_name.end(), '-', '_');
                    
                    fs::path dest = target_presets / plugin_folder_name;
                    // Копируем с обновлением существующих файлов
                    fs::copy(entry.path(), dest, fs::copy_options::recursive | fs::copy_options::update_existing);
                    std::cout << "Copied presets for: " << plugin_folder_name << std::endl;
                }
            }
        } catch(const std::exception& e) {
            std::cerr << "Failed to copy presets: " << e.what() << std::endl;
        }

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

    // 3. Создаем движок Lua (но пока НЕ загружаем скрипты!)
    LuaEngine lua_engine;

    // 4. Настройка базовых параметров и создание Ядра
    // ВАЖНО (RAII): Ядро объявляется ДО плагинов. 
    // Оно будет жить дольше всех и разрушится самым последним.
    WallpaperConfig wallpaper_config;
    wallpaper_config.output_name = "*";
    wallpaper_config.interactive = true; // Заглушка, реальное значение прочитаем позже
    
    InteractiveWallpaper wallpaper(wallpaper_config, lua_engine);

    // 5. Биндим API Ядра в Lua (теперь доступны core.set_interval, core.set_string и др.)
    // Делаем это ДО загрузки файлов, чтобы скрипты могли сразу использовать таймеры!
    lua_engine.bind_core_api(&wallpaper);

    // 6. ТЕПЕРЬ безопасно загружаем конфигурацию через Lua
    if (!lua_engine.load()) {
        std::cerr << "Failed to load Lua configuration!" << std::endl;
        // Не выходим, попытаемся запуститься с дефолтными значениями
    }

    // 7. Инициализация менеджера плагинов
    // ВАЖНО (RAII): При выходе из main он разрушится ПЕРВЫМ, 
    // корректно отписавшись от еще живого Ядра (epoll/BlackBoard).
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

    wallpaper.set_plugin_manager(&plugin_manager, effect_name);
    
    // 8. Подключение к Wayland и запуск провайдеров
    if (!wallpaper.initialize()) {
        std::cerr << "Failed to initialize Wayland compositor connection" << std::endl;
        return 1;
    }

    // Инициализируем провайдеры, передавая лямбду, которая применит настройки из Lua
    plugin_manager.initialize_providers(&wallpaper, [&lua_engine](IDataProviderABI* p) {
        return lua_engine.configure_provider(p);
    });

    // 9. Главный цикл (Zero-Latency Epoll Loop)
    std::cout << "Starting wallpaper main loop..." << std::endl;
    wallpaper.run();
    
    return 0;
}