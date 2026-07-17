#ifndef ICO_SPHERE_EFFECT_HPP
#define ICO_SPHERE_EFFECT_HPP

// Inherit from the SDK's Kinematic base class to automatically gain 
// 3D physics, inertia, center of mass (pivot), and Global Camera integration.
#include <shader-desk/kinematic-effect.hpp>

#include <GLES3/gl3.h>
#include <vector>
#include <string>
#include <map>

// ==============================================================================
// ICOSPHERE EFFECT (LEGACY VERSION)
// Generates an icosphere with dynamic subdivision and deforms its vertices 
// in the vertex shader based on audio telemetry (FFTW) and 3D Simplex noise.
// ==============================================================================
class IcoSphereEffect : public KinematicEffect {
public:
    IcoSphereEffect();
    ~IcoSphereEffect() override;

    // --- Core API Implementation ---
    const char* get_name() const override;
    std::vector<EffectParameter> get_parameters() const override;
    void set_parameter(const std::string& name, const EffectParameterValue& value) override;

    bool initialize(ICoreContext* core, uint32_t width, uint32_t height) override;
    void render(uint32_t width, uint32_t height, float dt) override;
    void cleanup() override;

private:
    // --- Shader Management ---
    std::string active_shader = "default"; 
    bool needs_shader_reload = true;       
    
    bool reload_shader_program();          
    void fetch_uniform_locations();        

    GLuint program = 0;
    GLuint vao = 0, vbo = 0, ebo = 0, line_ebo = 0;

    // --- BlackBoard Pointers (Audio Telemetry) ---
    float* p_audio_bands = nullptr;
    float* p_audio_bass = nullptr;
    float* p_audio_mid = nullptr;
    float* p_audio_treble = nullptr;

    // --- Internal State & Geometry ---
    int subdivisions = 3;
    bool needs_regeneration = false;
    float time = 0.0f;
    bool wireframe_mode = true;

    std::vector<glm::vec3> vertices;
    std::vector<unsigned int> indices;
    std::vector<float> phases;
    std::vector<glm::vec3> normals;
    std::vector<unsigned int> line_indices;

    void generate_icosphere(int subdivisions);
    void update_buffers();
    void update_effect_scaling();

    // --- Visual Parameters ---
    float oscill_amp = 0.0f, oscill_freq = 1.0f;
    float wave_amp = 0.0f, wave_freq = 1.0f;
    float twist_amp = 0.0f, pulse_amp = 0.0f, noise_amp = 0.0f;
    float scaled_oscill_amp = 0.0f, scaled_wave_amp = 0.0f;
    float scaled_twist_amp = 0.0f, scaled_pulse_amp = 0.0f, scaled_noise_amp = 0.0f;
    
    float sphere_scale = 1.0f;
    glm::vec3 background_color = {0.1137f, 0.1137f, 0.1255f}; // Used as object_color
    glm::vec3 wireframe_color = {0.5f, 0.5f, 0.7f};

    // --- OpenGL Uniform Locations ---
    GLuint u_lightColor = 0, u_lightPos = 0, u_viewPos = 0;
    GLuint u_model = 0, u_view = 0, u_projection = 0, u_time = 0;
    GLuint u_wireframe_color = 0, u_object_color = 0, u_is_wireframe_pass = 0;
    GLuint u_oscill_amp = 0, u_oscill_freq = 0, u_wave_amp = 0, u_wave_freq = 0;
    GLuint u_twist_amp = 0, u_pulse_amp = 0, u_noise_amp = 0;
    GLuint u_sphere_scale = 0;
    
    GLuint u_audio_bass = 0, u_audio_mid = 0, u_audio_treble = 0;
    GLuint u_audio_bands = 0;
};

#endif // ICO_SPHERE_EFFECT_HPP