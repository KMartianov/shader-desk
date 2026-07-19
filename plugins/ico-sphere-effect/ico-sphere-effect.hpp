// Src/ico-sphere-effect.hpp
#ifndef ICO_SPHERE_EFFECT_HPP
#define ICO_SPHERE_EFFECT_HPP

#include <shader-desk/kinematic-effect.hpp> // <-- ВАЖНО: Подключаем базовый класс физики
#include <GLES3/gl3.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <string>
#include <array>
#include <memory>

/**
 * @brief Vertex structure for barycentric rendering.
 */
struct Vertex {
    glm::vec3 position;    // Layout (location = 0)
    float phase;           // Layout (location = 1)
    glm::vec3 normal;      // Layout (location = 2)
    glm::vec3 barycentric; // Layout (location = 3)
};

struct BloomResources {
    GLuint fbo_scene = 0;
    GLuint tex_scene_color = 0;
    GLuint tex_scene_bright = 0; 
    GLuint rbo_depth = 0;

    GLuint fbo_pingpong[2] = {0, 0};
    GLuint tex_pingpong[2] = {0, 0};

    uint32_t width = 0;
    uint32_t height = 0;
    bool initialized = false;
};

// Наследуемся от KinematicEffect для интеграции с Глобальной Камерой и физикой
class IcoSphereEffect : public KinematicEffect {
public:
    IcoSphereEffect();
    ~IcoSphereEffect() override;

    bool initialize(ICoreContext* core, uint32_t width, uint32_t height) override;
    void render(uint32_t width, uint32_t height, float dt) override;
    void on_cleanup() override;
    
    // --- SETTINGS API ---
    void set_wireframe_mode(bool enabled) { wireframe_mode = enabled; }
    void set_subdivisions(int value);
    void set_background_color(const glm::vec3& color) { background_color = color; }
    void set_wireframe_color(const glm::vec3& color) { wireframe_color = color; }
    void set_object_color(const glm::vec3& color) { object_color = color; }

    void set_oscill_amp(float value) { oscill_amp = value; update_effect_scaling(); }
    void set_oscill_freq(float value) { oscill_freq = value; }
    void set_wave_amp(float value) { wave_amp = value; update_effect_scaling(); }
    void set_wave_freq(float value) { wave_freq = value; }
    void set_twist_amp(float value) { twist_amp = value; update_effect_scaling(); }
    void set_pulse_amp(float value) { pulse_amp = value; update_effect_scaling(); }
    void set_noise_amp(float value) { noise_amp = value; update_effect_scaling(); }

    void set_sphere_scale(float scale);
    float get_sphere_scale() const { return sphere_scale; }

    void set_bloom_intensity(float intensity) { bloom_intensity = std::max(0.0f, intensity); }
    void set_matcap_texture(const std::string& filename);

    void set_use_instancing(bool enabled) { use_instancing = enabled; }
    void set_inner_scale(float scale) { inner_scale = scale; }
    void set_outer_scale(float scale) { outer_scale = scale; }

    void trigger_shockwave(const glm::vec3& hit_point);

protected:
    void update_effect_scaling();

    std::string active_shader = "default";
    bool needs_shader_reload = true;
    bool reload_shader_program();
    void fetch_uniform_locations();

    void generate_icosphere(int subdivisions);
    void update_buffers();
    void update_shockwaves(float dt);

    bool init_bloom_resources(uint32_t width, uint32_t height);
    void destroy_bloom_resources();
    void render_bloom_postprocess(uint32_t width, uint32_t height);
    void load_matcap_from_file(const std::string& path);

    // --- STATE VARIABLES ---
    GLuint program = 0;
    GLuint vao = 0, vbo = 0, ebo = 0;

    GLuint bloom_blur_program = 0;
    GLuint bloom_final_program = 0;
    BloomResources bloom;
    float bloom_intensity = 0.5f;

    GLuint matcap_texture_id = 0;
    std::string matcap_filename = "";
    bool use_matcap = false;

    bool use_instancing = false;
    float inner_scale = 0.8f;
    float outer_scale = 1.2f;

    std::array<glm::vec4, 4> shockwaves;
    uint32_t current_shockwave_idx = 0;

    float* p_audio_bass = nullptr;
    float* p_audio_mid = nullptr;
    float* p_audio_treble = nullptr;
    float* p_audio_bands = nullptr; 

    int subdivisions = 3;
    bool needs_regeneration = false;
    float time = 0.0f;
    bool wireframe_mode = true;
    float sphere_scale = 1.0f;

    float oscill_amp = 0.0f, oscill_freq = 1.0f;
    float wave_amp = 0.0f, wave_freq = 1.0f;
    float twist_amp = 0.0f, pulse_amp = 0.0f, noise_amp = 0.0f;
    
    float scaled_oscill_amp, scaled_wave_amp;
    float scaled_twist_amp, scaled_pulse_amp, scaled_noise_amp;

    glm::vec3 background_color = {0.1137f, 0.1137f, 0.1255f};
    glm::vec3 wireframe_color  = {0.5f, 0.5f, 0.7f};
    glm::vec3 object_color     = {0.08f, 0.12f, 0.20f};

    GLuint u_model = 0, u_view = 0, u_projection = 0, u_time = 0;
    GLuint u_lightColor = 0, u_lightPos = 0, u_viewPos = 0;
    GLuint u_wireframe_color = 0, u_object_color = 0, u_is_wireframe_pass = 0;
    GLuint u_oscill_amp = 0, u_oscill_freq = 0, u_wave_amp = 0, u_wave_freq = 0;
    GLuint u_twist_amp = 0, u_pulse_amp = 0, u_noise_amp = 0, u_sphere_scale = 0;
    GLuint u_audio_bass = 0, u_audio_mid = 0, u_audio_treble = 0, u_audio_bands = 0;
    GLuint u_shockwaves = 0;
    GLuint u_use_matcap = 0, u_matcap_tex = 0;
    GLuint u_use_instancing = 0, u_inner_scale = 0, u_outer_scale = 0;

    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
};

#endif // ICO_SPHERE_EFFECT_HPP