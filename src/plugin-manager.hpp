#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional> // Добавлено для std::function
#include "wallpaper-effect.hpp" 
#include "data-provider.hpp"    

class PluginManager {
public:
    PluginManager(const std::string& plugin_dir);
    ~PluginManager();

    // Сканирует директорию на наличие .so файлов и загружает их
    void discover_plugins();
    
    /**
     * @brief Инициализирует найденные Data Providers.
     * @param core Контекст ядра (epoll, BlackBoard).
     * @param configure_callback Коллбэк для конфигурации. Должен прочитать настройки из Lua,
     *                           передать их через provider->set_parameter() и вернуть true.
     *                           Если в Lua стоит enabled=false, должен вернуть false.
     */
    void initialize_providers(ICoreContext* core, const std::function<bool(IDataProvider*)>& configure_callback = nullptr);

    // Фабрика для визуальных эффектов (создает новый экземпляр по имени)
    WallpaperEffectPtr create_effect(const std::string& effect_name);
    
    // Возвращает список имен доступных визуальных эффектов
    std::vector<std::string> get_available_effects() const;

private:
    struct PluginHandle;
    
    // Хранилище фабрик визуальных эффектов
    std::vector<std::unique_ptr<PluginHandle>> loaded_plugins;
    
    // Хранилище АКТИВНЫХ провайдеров данных (с кастомным удалителем из .so)
    std::vector<std::unique_ptr<IDataProvider, void(*)(IDataProvider*)>> data_providers;
    
    // Хранилище дескрипторов dlopen для Data Providers.
    std::vector<void*> provider_handles;

    std::string plugin_directory;
};