// src/ico-sphere-effect.hpp

#ifndef ICO_SPHERE_EFFECT_HPP
#define ICO_SPHERE_EFFECT_HPP

#include <GLES3/gl3.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp> 
#include <vector>
#include <string>
#include <random>
#include "wallpaper-effect.hpp"

class IcoSphereEffect : public WallpaperEffect {
public:
    IcoSphereEffect();

    // --- Реализация интерфейса WallpaperEffect ---
    bool initialize(uint32_t width, uint32_t height) override;
    void render(uint32_t width, uint32_t height) override;
    void cleanup() override;

    void handle_audio_data(const AudioData& data) override; 
    void handle_pointer_motion(double dx, double dy, bool is_touchpad) override;
    
    // --- Методы для конфигурации ---
    void set_wireframe_mode(bool enabled) { wireframe_mode = enabled; }
    void set_subdivisions(int value);

    void set_oscill_amp(float value) { oscill_amp = value; update_effect_scaling(); }
    void set_oscill_freq(float value) { oscill_freq = value; }
    void set_wave_amp(float value) { wave_amp = value;  update_effect_scaling(); }
    void set_wave_freq(float value) { wave_freq = value; }
    void set_twist_amp(float value) { twist_amp = value; update_effect_scaling(); }
    void set_pulse_amp(float value) { pulse_amp = value; update_effect_scaling(); }
    void set_noise_amp(float value) { noise_amp = value; update_effect_scaling(); }

    void set_background_color(const glm::vec3& color) { background_color = color; }
    void set_wireframe_color(const glm::vec3& color) { wireframe_color = color; }
    void set_mouse_sensitivity(float value) { mouse_sensitivity = value; }
    void set_touchpad_sensitivity(float value) { touchpad_sensitivity = value; }

    void set_constant_rotation_speed(float value) { constant_rotation_speed = value; }
    void set_rotation_decay(float value) { rotation_decay = value; }
    void set_min_rotation_speed(float value) { min_rotation_speed = value; }
    void set_max_rotation_speed(float value) { max_rotation_speed = value; }
    void set_audio_reactive(bool enabled) { audio_reactive = enabled; }
    void set_audio_smoothing(float value) { audio_smoothing = value; }
    void set_bass_multiplier(float value) { bass_multiplier = value; }

    void update_effect_scaling();
    
    void set_sphere_scale(float scale) { 
        if (std::abs(sphere_scale - scale) > 0.001f) {
            sphere_scale = scale;
            update_effect_scaling();
        }
    }
    float get_sphere_scale() const { return sphere_scale; }

protected:
    GLuint program = 0;
    GLuint vao = 0, vbo = 0, ebo = 0, line_ebo = 0;
    
    glm::quat orientation;
    glm::vec3 angular_velocity;
    float rotation_decay = 0.95f;
    float constant_rotation_speed = 0.1f;

    float mouse_sensitivity;
    float touchpad_sensitivity;

    int subdivisions = 3;
    bool needs_regeneration = false;
    float time = 0.0f;
    bool wireframe_mode = true;

    float oscill_amp;
    float oscill_freq;
    float wave_amp;
    float wave_freq;
    float twist_amp;
    float pulse_amp;
    float noise_amp;

    float scaled_oscill_amp;
    float scaled_oscill_freq;
    float scaled_wave_amp;
    float scaled_wave_freq;
    float scaled_twist_amp;
    float scaled_pulse_amp;
    float scaled_noise_amp;
    
    GLuint u_model, u_view, u_projection, u_time;
    GLuint u_wireframe_color, u_background_color, u_is_wireframe_pass;
    GLuint u_oscill_amp, u_oscill_freq, u_wave_amp, u_wave_freq;
    GLuint u_twist_amp, u_pulse_amp, u_noise_amp;

    GLuint u_sphere_scale;

    glm::vec3 background_color = {0.1137f, 0.1137f, 0.1255f};
    glm::vec3 wireframe_color = {0.5f, 0.5f, 0.7f};

    std::vector<glm::vec3> vertices;
    std::vector<unsigned int> indices;
    std::vector<float> phases;
    std::vector<glm::vec3> normals;
    std::vector<unsigned int> line_indices;

    float sphere_scale = 1.0f;

    void generate_icosphere(int subdivisions);
    void update_buffers();
    void update_rotation(float dt); 

    float min_rotation_speed = 0.001f;
    float max_rotation_speed = 5.0f;

    // Аудио-реактивность
    bool audio_reactive;
    float audio_smoothing;
    float bass_multiplier;
    float smoothed_bass;
    float smoothed_mid;
    float smoothed_treble;
    GLuint u_audio_bass;
    GLuint u_audio_mid;
    GLuint u_audio_treble;
};

#endif // ICO_SPHERE_EFFECT_HPP