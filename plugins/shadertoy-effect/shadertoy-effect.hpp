// Src/plugins/shadertoy-effect/shadertoy-effect.hpp

#pragma once

#include <shader-desk/wallpaper-effect.hpp>
#include <GLES3/gl3.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>

// Supported ShaderToy channel types. 
// Can be expanded in the future (e.g., CUBEMAP, AUDIO_FFT).
enum class ChannelType { 
    NONE, 
    TEXTURE 
};

// Tracks the state and lifecycle of an iChannel (e.g., iChannel0)
struct ShaderToyChannel {
    ChannelType type = ChannelType::NONE;
    std::string source_file;
    GLuint texture_id = 0;
    
    // Architectural Flag: Indicates that Lua requested a texture change,
    // but the blocking file I/O and OpenGL generation must be deferred 
    // until the active EGL context is guaranteed to be bound in the render loop.
    bool pending_load = false; 
    
    GLuint uniform_location = 0;
};

// Tracks dynamic uniforms parsed from the shader's // @param comments.
// Bridges the gap between the Lua Control Plane and the GLSL Pipeline.
struct DynamicParam {
    std::string name;
    std::string description;
    EffectParameterValue value; // std::variant managed by the ShaderDesk SDK
    GLuint uniform_location = 0;
};

class ShaderToyEffect : public WallpaperEffect {
public:
    ShaderToyEffect() = default;
    ~ShaderToyEffect() override { cleanup(); }

    const char* get_name() const override { return "ShaderToy Sandbox"; }
    
    // --- Parameter Management (Lua API Integration) ---
    std::vector<EffectParameter> get_parameters() const override;
    void set_parameter(const std::string& name, const EffectParameterValue& value) override;

    // --- Core Lifecycle ---
    bool initialize(ICoreContext* core, uint32_t width, uint32_t height) override;
    void render(uint32_t width, uint32_t height, float dt) override;
    void cleanup() override;

private:
    // --- OpenGL State ---
    GLuint program = 0;
    GLuint vao = 0;
    
    // --- Playback State ---
    float time_accum = 0.0f;
    int frame_count = 0;

    // --- Microkernel Context ---
    // Stored to resolve bundle paths dynamically during deferred texture loading.
    ICoreContext* m_core = nullptr; 
    
    // --- Zero-Latency Data Bus (BlackBoard) ---
    // Direct memory pointers for hardware mouse tracking.
    float* p_mouse_x = nullptr;
    float* p_mouse_y = nullptr;

    // --- Standard ShaderToy Uniform Locations ---
    GLuint u_iResolution = 0;
    GLuint u_iTime = 0;
    GLuint u_iTimeDelta = 0;
    GLuint u_iFrame = 0;
    GLuint u_iMouse = 0;

    

    // --- Dynamic Pipeline State ---
    std::string target_shader_file = "st_demo_frag.glsl";
    bool pending_recompile = false;
    std::vector<DynamicParam> dynamic_params;
    
    // Supports standard ShaderToy inputs (iChannel0 through iChannel3)
    ShaderToyChannel channels[4]; 

    // --- Internal Utilities ---
    bool parse_and_compile(ICoreContext* core);
    void extract_metadata_from_source(const std::string& source);
    EffectParameterValue parse_default_value(const std::string& type_str, const std::string& val_str);
    
    // Executes blocking disk I/O and texture generation safely within the EGL context.
    void process_pending_textures();
};