#include "postprocess-effect.hpp"
#include <iostream>
#include <algorithm>

const char* PostprocessEffectEffect::get_name() const { return "Postprocess Effect"; }

std::vector<EffectParameter> PostprocessEffectEffect::get_parameters() const {
    std::vector<EffectParameter> params = {
        {"shader_theme", "Shader variation name (e.g. 'default')", active_shader},
        {"intensity", "Glitch and RGB split intensity", intensity},
        {"speed", "Artifact animation speed", speed},
        {"scale", "Glitch block density", scale},
        {"variant", "0: Smooth RGB, 1: Block VHS, 2: Hardcore Scanlines", variant}
    };
    register_standard_params(params);
    return params;
}

void PostprocessEffectEffect::set_parameter(const std::string& name, const EffectParameterValue& value) {
    if (apply_standard_param(name, value)) return;

    try {
        if (name == "shader_theme") {
            std::string new_theme = std::get<std::string>(value);
            if (new_theme != active_shader) {
                active_shader = new_theme;
                // Seamlessly swap shaders on the next frame (Shadow-Commit Architecture)
                request_shader_reload(active_shader + "_frag.glsl", active_shader + "_vert.glsl");
            }
        }
        else if (name == "intensity") intensity = std::get<float>(value);
        else if (name == "speed") speed = std::get<float>(value);
        else if (name == "scale") scale = std::get<float>(value);
        else if (name == "variant") variant = std::get<int>(value);
    } catch (const std::bad_variant_access& e) {
        std::cerr << "[" << get_name() << "] Warning: Type mismatch for '" << name << "'\n";
    }
}

bool PostprocessEffectEffect::initialize(ICoreContext* core, uint32_t width, uint32_t height) {
    bool success = init_standard_pipeline(core, active_shader + "_vert.glsl", active_shader + "_frag.glsl");
    if (success) {
        std::cout << "\033[32m[" << get_name() << "] Standard pipeline initialized.\033[0m\n";
    }
    return success;
}

void PostprocessEffectEffect::bind_custom_uniforms() {
    GLint prog;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prog);

    // CRITICAL: Explicitly bind the base layer FBO texture to sampler 0
    // The base class binds the actual texture object to GL_TEXTURE0.
    glUniform1i(glGetUniformLocation(prog, "u_prev_layer"), 0);

    glUniform1f(glGetUniformLocation(prog, "intensity"), intensity);
    glUniform1f(glGetUniformLocation(prog, "speed"), speed);
    glUniform1f(glGetUniformLocation(prog, "scale"), scale);
    glUniform1i(glGetUniformLocation(prog, "variant"), variant);
}

extern "C" {
    uint32_t get_abi_version() { return SHADER_DESK_ABI_VERSION; }
    IWallpaperEffectABI* create_effect() { return new PostprocessEffectEffect(); }
    void destroy_effect(IWallpaperEffectABI* effect) { delete static_cast<WallpaperEffect*>(effect); }
}