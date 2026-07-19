#include "solid-bg.hpp"
#include <iostream>
#include <algorithm>

const char* SolidBgEffect::get_name() const { return "Solid Bg"; }

std::vector<EffectParameter> SolidBgEffect::get_parameters() const {
    std::vector<EffectParameter> params = {
        {"gradient_type", "0: Solid, 1: Linear, 2: Radial, 3: 4-Corner Mesh", gradient_type},
        {"color_space", "0: sRGB (Standard), 1: Oklab (Perceptual smooth blending)", color_space},
        {"color_1", "Primary Color (Top-Left)", color_1},
        {"color_2", "Secondary Color (Bottom-Right)", color_2},
        {"color_3", "Tertiary Color (Top-Right / Mesh only)", color_3},
        {"color_4", "Quaternary Color (Bottom-Left / Mesh only)", color_4},
        {"angle", "Angle in degrees (Linear only)", angle},
        {"radial_center", "Center position (Radial only)", radial_center},
        {"radial_radius", "Gradient spread radius (Radial only)", radial_radius},
        {"dither_strength", "TPDF Dithering intensity (1.0 = smooth 8-bit)", dither_strength}
    };
    
    // Injects 'layer_fbo_scale' support seamlessly
    register_standard_params(params); 
    return params;
}

void SolidBgEffect::set_parameter(const std::string& name, const EffectParameterValue& value) {
    // Let the base class intercept pipeline-specific parameters
    if (apply_standard_param(name, value)) return;

    try {
        if (name == "gradient_type") gradient_type = std::get<int>(value);
        else if (name == "color_space") color_space = std::get<int>(value);
        else if (name == "color_1") color_1 = std::get<glm::vec3>(value);
        else if (name == "color_2") color_2 = std::get<glm::vec3>(value);
        else if (name == "color_3") color_3 = std::get<glm::vec3>(value);
        else if (name == "color_4") color_4 = std::get<glm::vec3>(value);
        else if (name == "angle") angle = std::get<float>(value);
        else if (name == "radial_center") radial_center = std::get<glm::vec2>(value);
        else if (name == "radial_radius") radial_radius = std::get<float>(value);
        else if (name == "dither_strength") dither_strength = std::get<float>(value);
    } catch (const std::bad_variant_access& e) {
        std::cerr << "[" << get_name() << "] Warning: Type mismatch for parameter '" << name << "'\n";
    }
}

bool SolidBgEffect::initialize(ICoreContext* core, uint32_t width, uint32_t height) {
    bool success = init_standard_pipeline(core, "solid_vert.glsl", "solid_frag.glsl");
    if (success) {
        std::cout << "\033[32m[" << get_name() << "] Standard pipeline initialized.\033[0m\n";
    }
    return success;
}

void SolidBgEffect::bind_custom_uniforms() {
    GLint prog;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prog);

    // O(1) string hashing lookup in modern OpenGL drivers. Zero CPU allocation.
    glUniform1i(glGetUniformLocation(prog, "gradient_type"), gradient_type);
    glUniform1i(glGetUniformLocation(prog, "color_space"), color_space);
    glUniform3fv(glGetUniformLocation(prog, "color_1"), 1, &color_1[0]);
    glUniform3fv(glGetUniformLocation(prog, "color_2"), 1, &color_2[0]);
    glUniform3fv(glGetUniformLocation(prog, "color_3"), 1, &color_3[0]);
    glUniform3fv(glGetUniformLocation(prog, "color_4"), 1, &color_4[0]);
    glUniform1f(glGetUniformLocation(prog, "angle"), angle);
    glUniform2fv(glGetUniformLocation(prog, "radial_center"), 1, &radial_center[0]);
    glUniform1f(glGetUniformLocation(prog, "radial_radius"), radial_radius);
    glUniform1f(glGetUniformLocation(prog, "dither_strength"), dither_strength);
}

extern "C" {
    uint32_t get_abi_version() { return SHADER_DESK_ABI_VERSION; }
    IWallpaperEffectABI* create_effect() { return new SolidBgEffect(); }
    void destroy_effect(IWallpaperEffectABI* effect) { delete static_cast<WallpaperEffect*>(effect); }
}