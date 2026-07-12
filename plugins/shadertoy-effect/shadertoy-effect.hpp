#pragma once
#include <shader-desk/wallpaper-effect.hpp>
#include <GLES3/gl3.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>

// Dynamic parameter storage
struct DynamicParam {
    std::string name;
    std::string description;
    EffectParameterValue value; // Std::variant from SDK
    GLuint uniform_location = 0;
};

class ShaderToyEffect : public WallpaperEffect {
public:
    ShaderToyEffect() = default;
    ~ShaderToyEffect() override { cleanup(); }

    const char* get_name() const override { return "ShaderToy Sandbox"; }
    
    // --- Parameters Interface ---
    std::vector<EffectParameter> get_parameters() const override;
    void set_parameter(const std::string& name, const EffectParameterValue& value) override;

    // --- Lifecycle ---
    bool initialize(ICoreContext* core, uint32_t width, uint32_t height) override;
    void render(uint32_t width, uint32_t height, float dt) override;
    void cleanup() override;

private:
    GLuint program = 0;
    GLuint vao = 0;
    float time_accum = 0.0f;
    int frame_count = 0;

    // --- BlackBoard Pointers ---
    float* p_mouse_x = nullptr;
    float* p_mouse_y = nullptr;

    // --- Standard ShaderToy Uniforms ---
    GLuint u_iResolution = 0;
    GLuint u_iTime = 0;
    GLuint u_iTimeDelta = 0;
    GLuint u_iFrame = 0;
    GLuint u_iMouse = 0;

    // --- Dynamic User Uniforms ---
    std::string target_shader_file = "st_demo_frag.glsl";
    std::vector<DynamicParam> dynamic_params;

    // Utilities
    bool parse_and_compile(ICoreContext* core);
    void extract_parameters_from_source(const std::string& source);
    EffectParameterValue parse_default_value(const std::string& type_str, const std::string& val_str);
};