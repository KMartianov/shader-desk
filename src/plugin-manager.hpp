// src/plugin-manager.hpp
#pragma once

#include <string>
#include <vector>
#include <memory>
#include "wallpaper-effect.hpp" 
#include "data-provider.hpp"    

class PluginManager {
public:
    PluginManager(const std::string& plugin_dir);
    ~PluginManager();

    // Сканирует директорию на наличие .so файлов и загружает их
    void discover_plugins();
    
    // Инициализирует все найденные Data Providers, подключая их к Ядру
    void initialize_providers(ICoreContext* core);

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
    // Гарантирует, что .so файл не выгрузится из памяти до уничтожения объектов.
    std::vector<void*> provider_handles;

    std::string plugin_directory;
};