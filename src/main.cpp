// main.cpp (полная версия с поддержкой subdivisions)
#include "interactive-wallpaper.hpp"
#include "shader-effect.hpp"
#include "ico-sphere-effect.hpp"
#include <iostream>
#include <memory>
#include <string>
#include <fstream>
#include <nlohmann/json.hpp>
#include "config-loader.hpp"
using json = nlohmann::json;



void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [effect_name]" << std::endl;
    std::cout << "Available effects: plasma, fire, water, spirals, sphere" << std::endl;
    std::cout << "Example: " << program_name << " sphere" << std::endl;
}

int main(int argc, char** argv) {
    // Загрузка конфигурации
    json config = load_config();
    
    WallpaperConfig wallpaper_config;
    wallpaper_config.output_name = "*";
    
    std::string effect_name = config.value("effect", "sphere");
    
    if (argc > 1) {
        effect_name = argv[1];
    }
    
    InteractiveWallpaper wallpaper(wallpaper_config);
    
    if (!wallpaper.initialize()) {
        std::cerr << "Failed to initialize wallpaper" << std::endl;
        return 1;
    }

    // Выбор эффекта с учетом конфигурации
    if (effect_name == "sphere") {
        auto effect = std::make_unique<IcoSphereEffect>();
        
        // Применение параметров из конфигурации
        effect->set_wireframe_mode(config.value("wireframe_mode", true));
        effect->set_subdivisions(config.value("subdivisions", 3));
        effect->set_oscill_amp(config.value("oscill_amp", 0.1f));
        effect->set_oscill_freq(config.value("oscill_freq", 2.0f));
        effect->set_wave_amp(config.value("wave_amp", 0.05f));
        effect->set_wave_freq(config.value("wave_freq", 8.0f));
        effect->set_twist_amp(config.value("twist_amp", 0.08f));
        effect->set_pulse_amp(config.value("pulse_amp", 0.03f));
        effect->set_noise_amp(config.value("noise_amp", 0.02f));

        effect->set_constant_rotation_speed(config.value("constant_rotation_speed", 0.1f));
        effect->set_rotation_decay(config.value("rotation_decay", 0.95f));
        effect->set_min_rotation_speed(config.value("min_rotation_speed", 0.001f));
        effect->set_max_rotation_speed(config.value("max_rotation_speed", 5.0f));



        if (config.contains("touchpad_sensitivity")) {
            effect->set_touchpad_sensitivity(config.value("touchpad_sensitivity", 0.3f));
        }
    
        if (config.contains("mouse_sensitivity")) {
            effect->set_mouse_sensitivity(config.value("mouse_sensitivity", 2.5f));
        }
        if (config.contains("sphere_scale")) {
            float sphere_scale = config.value("sphere_scale", 1.0f);
            effect->set_sphere_scale(sphere_scale);
            std::cout << "  - Sphere scale: " << sphere_scale << std::endl;
        }


        
        // Цвета
        if (config.contains("background_color") && config["background_color"].is_array()) {
            glm::vec3 bg_color = {
                config["background_color"][0].get<float>(),
                config["background_color"][1].get<float>(),
                config["background_color"][2].get<float>()
            };
            effect->set_background_color(bg_color);
        }
        
        if (config.contains("wireframe_color") && config["wireframe_color"].is_array()) {
            glm::vec3 wf_color = {
                config["wireframe_color"][0].get<float>(),
                config["wireframe_color"][1].get<float>(),
                config["wireframe_color"][2].get<float>()
            };
            effect->set_wireframe_color(wf_color);
        }
        
        wallpaper.set_effect("*", std::move(effect));
        std::cout << "Starting with IcoSphere effect (subdivisions: " 
                  << config.value("subdivisions", 3) << ")" << std::endl;
    } else {
        std::string vert_path = "shaders/common.vert";
        std::string frag_path;
        
        if (effect_name == "plasma") {
            frag_path = "shaders/plasma.frag";
        } else if (effect_name == "fire") {
            frag_path = "shaders/fire.frag";
        } else if (effect_name == "water") {
            frag_path = "shaders/water.frag";
        } else if (effect_name == "spirals") {
            frag_path = "shaders/spirals.frag";
        } else {
            std::cerr << "Unknown effect: " << effect_name << std::endl;
            print_usage(argv[0]);
            return 1;
        }
        
        wallpaper.set_effect("*", std::make_unique<ShaderEffect>(vert_path, frag_path));
        std::cout << "Starting with shader effect: " << effect_name << std::endl;
    }
    
    std::cout << "Interactive wallpaper started with effect: " << effect_name << std::endl;
    std::cout << "Configuration:" << std::endl;
    std::cout << "  - Interactive: " << (config.value("interactive", true) ? "yes" : "no") << std::endl;
    
    if (effect_name == "sphere") {
        std::cout << "  - Wireframe mode: " << (config.value("wireframe_mode", true) ? "enabled" : "disabled") << std::endl;
        std::cout << "  - Subdivisions: " << config.value("subdivisions", 3) << std::endl;
        std::cout << "  - Oscillation amplitude: " << config.value("oscill_amp", 0.1) << std::endl;
        std::cout << "  - Wave amplitude: " << config.value("wave_amp", 0.05) << std::endl;
    }
    
    std::cout << "Press Ctrl+C to exit." << std::endl;
    
    wallpaper.run();
    
    return 0;
}