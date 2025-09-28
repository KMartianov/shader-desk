// config-loader.cpp
#include "config-loader.hpp"
#include <iostream>
#include <fstream>
#include <cstdlib>

std::string get_config_path() {
    return std::string(getenv("HOME")) + "/.config/interactive-wallpaper/config.json";
}

nlohmann::json load_config() {
    std::string config_path = get_config_path();
    std::ifstream config_file(config_path);
    
    if (!config_file.is_open()) {
        std::cerr << "Cannot open config file: " << config_path << std::endl;
        
        // Возвращаем конфигурацию по умолчанию
        return nlohmann::json::object();
    }
    
    try {
        return nlohmann::json::parse(config_file);
    } catch (const std::exception& e) {
        std::cerr << "Config parsing error: " << e.what() << std::endl;
        return nlohmann::json::object();
    }
}