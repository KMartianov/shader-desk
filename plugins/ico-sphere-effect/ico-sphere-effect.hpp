// Src/ico-sphere-effect.hpp
#ifndef ICO_SPHERE_EFFECT_HPP
#define ICO_SPHERE_EFFECT_HPP

#include <GLES3/gl3.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <string>
#include <array>
#include <memory>
#include "wallpaper-effect.hpp"

/**
 * @brief Vertex structure for barycentric rendering.
 * 
 * Enabling barycentric coordinates (aBary) allows the fragment shader
 * to draw a perfect wireframe over a solid color in a SINGLE Draw Call.
 * This completely eliminates Z-fighting (flickering at line-polygon intersections).
 */
struct Vertex {
    glm::vec3 position;    // Layout (location = 0)
    float phase;           // Layout (location = 1) - Random phase for organic noise
    glm::vec3 normal;      // Layout (location = 2)
    glm::vec3 barycentric; // Layout (location = 3) - (1,0,0), (0,1,0), (0,0,1) for triangle corners
};

/**
 * @brief Resources for Bloom post-processing (FBO).
 * 
 * FBO resolution is intentionally quartered (width/4, height/4) 
 * compared to the Wayland screen resolution. This provides a softer, 
 * "wider" neon glow and reduces laptop GPU load by 75%.
 */
struct BloomResources {
    GLuint fbo_scene = 0;
    GLuint tex_scene_color = 0;
    GLuint tex_scene_bright = 0; // Texture to extract only the bright (neon) areas
    GLuint rbo_depth = 0;

    // Ping-Pong buffers for two-pass Gaussian Blur
    GLuint fbo_pingpong[2] = {0, 0};
    GLuint tex_pingpong[2] = {0, 0};

    uint32_t width = 0;
    uint32_t height = 0;
    bool initialized = false;
};

/**
 * @brief High-performance visual plugin for Wayland compositors.
 * Supports audio-reactivity, mouse physics, MatCap, Bloom, and instancing.
 */
class IcoSphereEffect : public WallpaperEffect {
public:
    IcoSphereEffect();
    ~IcoSphereEffect() override;

    // --- WallpaperEffect Interface Implementation ---
    bool initialize(ICoreContext* core, uint32_t width, uint32_t height) override;
    void render(uint32_t width, uint32_t height, float dt) override;
    void cleanup() override;
    
    // ========================================================================
    // --- SETTINGS API (Lua Configuration Methods) ---
    // ========================================================================
    
    // Base visual parameters
    void set_wireframe_mode(bool enabled) { wireframe_mode = enabled; }
    void set_subdivisions(int value);
    void set_background_color(const glm::vec3& color) { background_color = color; }
    void set_wireframe_color(const glm::vec3& color) { wireframe_color = color; }
    void set_object_color(const glm::vec3& color) { object_color = color; }

    // Deformations and procedural organics
    void set_oscill_amp(float value) { oscill_amp = value; update_effect_scaling(); }
    void set_oscill_freq(float value) { oscill_freq = value; }
    void set_wave_amp(float value) { wave_amp = value; update_effect_scaling(); }
    void set_wave_freq(float value) { wave_freq = value; }
    void set_twist_amp(float value) { twist_amp = value; update_effect_scaling(); }
    void set_pulse_amp(float value) { pulse_amp = value; update_effect_scaling(); }
    void set_noise_amp(float value) { noise_amp = value; update_effect_scaling(); }

    // Rotation physics (Gyroscopic effect)
    void set_constant_rotation_speed(float value) { constant_rotation_speed = value; }
    void set_rotation_decay(float value) { rotation_decay = value; }
    void set_min_rotation_speed(float value) { min_rotation_speed = value; }
    void set_max_rotation_speed(float value) { max_rotation_speed = value; }

    // Scaling
    void set_sphere_scale(float scale);
    float get_sphere_scale() const { return sphere_scale; }

    // --- NEW PARAMETERS (New engine features) ---

    // 1. Bloom Post-processing
    void set_bloom_intensity(float intensity) { bloom_intensity = std::max(0.0f, intensity); }
    
    // 2. MatCap Texturing (Chrome, Gold, Pearl)
    void set_matcap_texture(const std::string& filename);

    // 3. Instancing ("Core in a cage")
    void set_use_instancing(bool enabled) { use_instancing = enabled; }
    void set_inner_scale(float scale) { inner_scale = scale; }
    void set_outer_scale(float scale) { outer_scale = scale; }

    // 4. Physical shockwaves
    // Called from Lua on a sharp bass spike to trigger a circular ripple across the sphere
    void trigger_shockwave(const glm::vec3& hit_point);

protected:
    ICoreContext* m_core = nullptr;
    void update_effect_scaling();

    // --- Shader management ---
    std::string active_shader = "default";
    bool needs_shader_reload = true;
    bool reload_shader_program();
    void fetch_uniform_locations();

    // --- Internal rendering and physics methods ---
    void generate_icosphere(int subdivisions);
    void update_buffers();
    void update_rotation(float dt);
    void update_shockwaves(float dt);

    // --- Bloom FBO and MatCap control methods ---
    bool init_bloom_resources(uint32_t width, uint32_t height);
    void destroy_bloom_resources();
    void render_bloom_postprocess(uint32_t width, uint32_t height);
    void load_matcap_from_file(const std::string& path);

    // ========================================================================
    // --- STATE VARIABLES AND OPENGL OBJECTS ---
    // ========================================================================

    // Main shader program (Sphere)
    GLuint program = 0;
    GLuint vao = 0, vbo = 0, ebo = 0;
    // Note: line_ebo is removed, since the barycentric wireframe is drawn via ebo (GL_TRIANGLES)

    // Shader programs for Bloom (Blur and Composition)
    GLuint bloom_blur_program = 0;
    GLuint bloom_final_program = 0;
    BloomResources bloom;
    float bloom_intensity = 0.5f;

    // MatCap texturing
    GLuint matcap_texture_id = 0;
    std::string matcap_filename = "";
    bool use_matcap = false;

    // Instancing
    bool use_instancing = false;
    float inner_scale = 0.8f;
    float outer_scale = 1.2f;

    // Rotation physics
    glm::quat orientation;
    glm::vec3 angular_velocity;
    float rotation_decay = 0.95f;
    float constant_rotation_speed = 0.1f;
    float min_rotation_speed = 0.001f;
    float max_rotation_speed = 5.0f;

    // Shockwaves
    // Xyz = impact coordinate on the sphere, w = time since impact (sec)
    std::array<glm::vec4, 4> shockwaves;
    uint32_t current_shockwave_idx = 0;

    // --- BlackBoard Pointers (Zero-Latency Data Bus) ---
    float* p_accum_x = nullptr;
    float* p_accum_y = nullptr;
    bool first_frame_mouse = true; 

    float last_mouse_x = 0.0f;
    float last_mouse_y = 0.0f;

    float* p_audio_bass = nullptr;
    float* p_audio_mid = nullptr;
    float* p_audio_treble = nullptr;
    float* p_audio_bands = nullptr; // Array of 64 equalizer frequencies

    // --- Sphere configuration parameters ---
    int subdivisions = 3;
    bool needs_regeneration = false;
    float time = 0.0f;
    bool wireframe_mode = true;
    float sphere_scale = 1.0f;

    float oscill_amp = 0.0f, oscill_freq = 1.0f;
    float wave_amp = 0.0f, wave_freq = 1.0f;
    float twist_amp = 0.0f, pulse_amp = 0.0f, noise_amp = 0.0f;
    
    // Scaled amplitudes (to preserve shape when sphere_scale changes)
    float scaled_oscill_amp, scaled_wave_amp;
    float scaled_twist_amp, scaled_pulse_amp, scaled_noise_amp;

    glm::vec3 background_color = {0.1137f, 0.1137f, 0.1255f};
    glm::vec3 wireframe_color  = {0.5f, 0.5f, 0.7f};
    glm::vec3 object_color     = {0.08f, 0.12f, 0.20f};

    // ========================================================================
    // --- UNIFORM LOCATIONS (Cached addresses in the shader) ---
    // ========================================================================
    
    // Standard matrices and lighting
    GLuint u_model = 0, u_view = 0, u_projection = 0, u_time = 0;
    GLuint u_lightColor = 0, u_lightPos = 0, u_viewPos = 0;
    
    // Colors and modes
    GLuint u_wireframe_color = 0, u_object_color = 0, u_is_wireframe_pass = 0;
    
    // Deformations
    GLuint u_oscill_amp = 0, u_oscill_freq = 0, u_wave_amp = 0, u_wave_freq = 0;
    GLuint u_twist_amp = 0, u_pulse_amp = 0, u_noise_amp = 0, u_sphere_scale = 0;
    
    // Audio and Shockwaves
    GLuint u_audio_bass = 0, u_audio_mid = 0, u_audio_treble = 0, u_audio_bands = 0;
    GLuint u_shockwaves = 0;

    // New features
    GLuint u_use_matcap = 0, u_matcap_tex = 0;
    GLuint u_use_instancing = 0, u_inner_scale = 0, u_outer_scale = 0;

    // Data buffers for CPU -> GPU
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
};

#endif // ICO_SPHERE_EFFECT_HPP