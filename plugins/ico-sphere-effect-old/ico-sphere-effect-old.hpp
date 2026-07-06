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
    bool initialize(ICoreContext* core, uint32_t width, uint32_t height) override;
    void render(uint32_t width, uint32_t height, float dt) override;
    void cleanup() override;
    
    // --- Configuration methods (Visual only) ---
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

    // Physics rotation settings
    void set_constant_rotation_speed(float value) { constant_rotation_speed = value; }
    void set_rotation_decay(float value) { rotation_decay = value; }
    void set_min_rotation_speed(float value) { min_rotation_speed = value; }
    void set_max_rotation_speed(float value) { max_rotation_speed = value; }

    void update_effect_scaling();
    
    void set_sphere_scale(float scale) { 
        if (std::abs(sphere_scale - scale) > 0.001f) {
            sphere_scale = scale;
            update_effect_scaling();
        }
    }
    float get_sphere_scale() const { return sphere_scale; }

protected:
    ICoreContext* m_core = nullptr;
    // --- Shader Management ---
    std::string active_shader = "default"; // Default shader name
    bool needs_shader_reload = true;       // Recompilation flag
    
    bool reload_shader_program();          // Shader loading function
    void fetch_uniform_locations();        // Uniform binding function

    GLuint program = 0;
    GLuint vao = 0, vbo = 0, ebo = 0, line_ebo = 0;
    
    glm::quat orientation;
    glm::vec3 angular_velocity;
    float rotation_decay = 0.95f;
    float constant_rotation_speed = 0.1f;
    float min_rotation_speed = 0.001f;
    float max_rotation_speed = 5.0f;

    // --- BlackBoard Pointers (Data from Providers) ---
    float* p_accum_x = nullptr;
    float* p_accum_y = nullptr;
    float last_mouse_x = 0.0f;
    float last_mouse_y = 0.0f;

    float* p_audio_bands = nullptr;


    float* p_audio_bass = nullptr;
    float* p_audio_mid = nullptr;
    float* p_audio_treble = nullptr;

    // --- Internal State ---
    int subdivisions = 3;
    bool needs_regeneration = false;
    float time = 0.0f;
    bool wireframe_mode = true;

    // --- Effect Parameters ---
    float oscill_amp, oscill_freq;
    float wave_amp, wave_freq;
    float twist_amp, pulse_amp, noise_amp;
    float scaled_oscill_amp, scaled_oscill_freq;
    float scaled_wave_amp, scaled_wave_freq;
    float scaled_twist_amp, scaled_pulse_amp, scaled_noise_amp;
    float sphere_scale = 1.0f;
    glm::vec3 background_color = {0.1137f, 0.1137f, 0.1255f};
    glm::vec3 wireframe_color = {0.5f, 0.5f, 0.7f};

    // --- Uniforms ---
    GLuint u_lightColor = 0, u_lightPos = 0, u_viewPos = 0;
    GLuint u_model, u_view, u_projection, u_time;
    GLuint u_wireframe_color, u_background_color, u_is_wireframe_pass;
    GLuint u_oscill_amp, u_oscill_freq, u_wave_amp, u_wave_freq;
    GLuint u_twist_amp, u_pulse_amp, u_noise_amp;
    GLuint u_sphere_scale;
    
    // Audio Uniforms
    GLuint u_audio_bass = 0;
    GLuint u_audio_mid = 0;
    GLuint u_audio_treble = 0;

    GLuint u_audio_bands = 0;

    std::vector<glm::vec3> vertices;
    std::vector<unsigned int> indices;
    std::vector<float> phases;
    std::vector<glm::vec3> normals;
    std::vector<unsigned int> line_indices;

    void generate_icosphere(int subdivisions);
    void update_buffers();
    void update_rotation(float dt); 
};

#endif // ICO_SPHERE_EFFECT_HPP