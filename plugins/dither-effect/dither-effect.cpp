// plugins/dither-effect/dither-effect.cpp
#include "dither-effect.hpp"
#include <iostream>
#include <stdexcept>
#include <algorithm>

// ==============================================================================
// DEVELOPER GUIDE: STANDARD RENDER PIPELINE
// ==============================================================================
// This plugin utilizes the new Standard Render Pipeline provided by the SDK.
// Notice how much boilerplate code has been eliminated:
// - No render() or cleanup() methods.
// - No manual VAO/VBO allocation.
// - No time tracking or resolution math.
// - No manual Hot-Reload logic.
// All of this is handled seamlessly by the `WallpaperEffect` base class.
// ==============================================================================

const char* DitherEffectEffect::get_name() const {
    return "Dither Effect";
}

// ------------------------------------------------------------------------------
// PARAMETER MANAGEMENT
// Exposes parameters to the Lua configuration engine.
// ------------------------------------------------------------------------------
std::vector<EffectParameter> DitherEffectEffect::get_parameters() const {
    std::vector<EffectParameter> params = {
        {"shader_theme", "Shader variation name (e.g. 'default')", active_shader},
        {"dither_spread", "Noise intensity (0.0 = banding, 1.0 = heavy noise)", dither_spread},
        {"downsample_scale", "Pixel size (1.0 = native, 4.0 = retro)", downsample_scale},
        {"bayer_size", "Bayer Matrix resolution: 0 = 2x2, 1 = 4x4, 2 = 8x8", bayer_size},
        {"colors_count", "Number of active colors in the palette (2 to 16)", colors_count},
    };

    // CRITICAL: We must call this helper from the base class.
    // It injects the `layer_fbo_scale` parameter, enabling users to downsample 
    // or upsample this specific effect directly from Lua to save GPU power.
    register_standard_params(params);
    
    return params;
}

void DitherEffectEffect::set_parameter(const std::string& name, const EffectParameterValue& value) {
    // 1. Intercept Standard Parameters
    // If the base class recognizes the parameter (e.g., "layer_fbo_scale"), 
    // it processes it internally and returns true.
    if (apply_standard_param(name, value)) return;

    // 2. Process Custom Plugin Parameters
    try {
        if (name == "shader_theme") {
            std::string new_theme = std::get<std::string>(value);
            if (new_theme != active_shader) {
                active_shader = new_theme;
                
                // Trigger the base class's Safe Shadow-Commit Hot-Reload mechanism.
                // It will attempt to compile the new files on the next frame.
                // If the user made a syntax error, it safely reverts to the old shader.
                request_shader_reload(active_shader + "_frag.glsl", active_shader + "_vert.glsl");
            }
        }
        else if (name == "dither_spread") {
            dither_spread = std::get<float>(value);
        }
        else if (name == "downsample_scale") {
            downsample_scale = std::get<float>(value);
        }
        else if (name == "bayer_size") {
            bayer_size = std::get<int>(value);
        }
        else if (name == "colors_count") {
            colors_count = std::get<int>(value);
        }
        else {
             std::cerr << "[" << get_name() << "] Warning: Unknown parameter '" << name << "'." << std::endl;
        }
    } catch (const std::bad_variant_access& e) {
        std::cerr << "[" << get_name() << "] Warning: Type mismatch for parameter '" 
                  << name << "'. " << e.what() << std::endl;
    }
}

// ------------------------------------------------------------------------------
// INITIALIZATION
// ------------------------------------------------------------------------------
bool DitherEffectEffect::initialize(ICoreContext* core, uint32_t width, uint32_t height) {
    // Initializes the complete OpenGL pipeline with a single call.
    // It loads the files, injects GLSL ES boilerplate (#version 300 es), 
    // compiles the program, sets up the Zero-VBO quad, and maps basic uniforms.
    bool success = init_standard_pipeline(core, active_shader + "_vert.glsl", active_shader + "_frag.glsl");
    
    if (success) {
        std::cout << "\033[32m[" << get_name() << "] Standard pipeline initialized successfully.\033[0m" << std::endl;
    }
    
    return success;
}

// ------------------------------------------------------------------------------
// RENDER HOOK: CUSTOM UNIFORMS
// ------------------------------------------------------------------------------
void DitherEffectEffect::bind_custom_uniforms() {
    GLint prog;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prog);

    // ==============================================================================
    // FBO PIPELINE ROUTING
    // ==============================================================================
    // Explicitly bind the sampler to GL_TEXTURE0. This guarantees that when this 
    // plugin operates as a Post-Process Blit Shader or a Nested Ping-Pong Filter, 
    // it correctly reads the rendered scene/object from the Wayland Microkernel.
    glUniform1i(glGetUniformLocation(prog, "u_prev_layer"), 0);

    // ==============================================================================
    // DYNAMIC UNIFORMS
    // ==============================================================================
    glUniform1f(glGetUniformLocation(prog, "dither_spread"), dither_spread);
    glUniform1f(glGetUniformLocation(prog, "downsample_scale"), downsample_scale);
    glUniform1i(glGetUniformLocation(prog, "bayer_size"), bayer_size);
    glUniform1i(glGetUniformLocation(prog, "colors_count"), colors_count);

    // ==============================================================================
    // BLACKBOARD DATA FETCHING (Zero-Latency)
    // ==============================================================================
    // We retrieve the dynamic color palette from the engine's central memory bus.
    // This allows Lua presets (like Pywal or Doom Palette) to push massive arrays 
    // of floats directly to the GPU in O(1) time without string parsing overhead.
    
    // 16 colors * 3 components (RGB) = 48 floats
    float* raw_palette = m_core_ptr->get_blackboard()->bind_float_array("dither.palette", 48);
    
    GLint u_palette = glGetUniformLocation(prog, "palette");
    if (u_palette != -1 && raw_palette) {
        // Bulk upload the entire vector array to the VRAM
        glUniform3fv(u_palette, 16, raw_palette);
    }
}

// ==============================================================================
// C-ABI EXPORTS (Plugin Manager Entry Points)
// This guarantees stable binary compatibility between the .so and the host core.
// ==============================================================================
extern "C" {
    uint32_t get_abi_version() {
        return SHADER_DESK_ABI_VERSION;
    }
    
    IWallpaperEffectABI* create_effect() {
        return new DitherEffectEffect();
    }

    void destroy_effect(IWallpaperEffectABI* effect) {
        // Ensure we cast back to the base class so the virtual destructor 
        // properly cleans up the FBOs and Shaders allocated by the Standard Pipeline.
        delete static_cast<WallpaperEffect*>(effect);
    }
}