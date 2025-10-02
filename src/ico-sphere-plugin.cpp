#include "ico-sphere-effect.hpp" // Ваш существующий класс с логикой сферы
#include "wallpaper-effect.hpp"  // Новый интерфейс плагина
#include <iostream>
#include <string>

// Класс-адаптер, который превращает IcoSphereEffect в плагин.
// Он наследует реализацию от IcoSphereEffect.
class IcoSphereEffectPlugin : public IcoSphereEffect {
public:
    IcoSphereEffectPlugin() = default;
    ~IcoSphereEffectPlugin() override = default;

    // --- Реализация интерфейса WallpaperEffect ---

    const char* get_name() const override {
        return "Icosahedron Sphere";
    }

    // Предоставляем список всех настраиваемых параметров.
    std::vector<EffectParameter> get_parameters() const override {
        return {
            {"wireframe_mode", "Render as a wireframe", wireframe_mode},
            {"subdivisions", "Level of sphere detail (0-6)", subdivisions},
            {"sphere_scale", "Overall size of the sphere", sphere_scale},
            
            {"oscill_amp", "Oscillation Amplitude", oscill_amp},
            {"oscill_freq", "Oscillation Frequency", oscill_freq},
            {"wave_amp", "Wave Amplitude", wave_amp},
            {"wave_freq", "Wave Frequency", wave_freq},
            {"twist_amp", "Twist Amplitude", twist_amp},
            {"pulse_amp", "Pulse Amplitude", pulse_amp},
            {"noise_amp", "Noise Amplitude", noise_amp},

            {"rotation_decay", "Inertia decay (0.9-1.0)", rotation_decay},
            {"max_rotation_speed", "Maximum rotation speed", max_rotation_speed},
            
            {"background_color", "Background clear color", background_color},
            {"wireframe_color", "Color of the wireframe lines", wireframe_color}
        };
    }

    // Универсальный метод для установки любого параметра извне.
    void set_parameter(const std::string& name, const EffectParameterValue& value) override {
        try {
            if (name == "wireframe_mode") { set_wireframe_mode(std::get<bool>(value)); }
            else if (name == "subdivisions") { set_subdivisions(std::get<int>(value)); }
            else if (name == "sphere_scale") { set_sphere_scale(std::get<float>(value)); }
            else if (name == "oscill_amp") { set_oscill_amp(std::get<float>(value)); }
            else if (name == "oscill_freq") { set_oscill_freq(std::get<float>(value)); }
            else if (name == "wave_amp") { set_wave_amp(std::get<float>(value)); }
            else if (name == "wave_freq") { set_wave_freq(std::get<float>(value)); }
            else if (name == "twist_amp") { set_twist_amp(std::get<float>(value)); }
            else if (name == "pulse_amp") { set_pulse_amp(std::get<float>(value)); }
            else if (name == "noise_amp") { set_noise_amp(std::get<float>(value)); }
            else if (name == "rotation_decay") { set_rotation_decay(std::get<float>(value)); }
            else if (name == "max_rotation_speed") { set_max_rotation_speed(std::get<float>(value)); }
            else if (name == "background_color") { set_background_color(std::get<glm::vec3>(value)); }
            else if (name == "wireframe_color") { set_wireframe_color(std::get<glm::vec3>(value)); }

        } catch (const std::bad_variant_access& e) {
            std::cerr << "Warning: Type mismatch for parameter '" << name << "'. " << e.what() << std::endl;
        }
    }
};

// --- Экспортируемые C-функции ---
extern "C" {
    WallpaperEffect* create_effect() {
        return new IcoSphereEffectPlugin();
    }

    void destroy_effect(WallpaperEffect* effect) {
        delete effect;
    }
}