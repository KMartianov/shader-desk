#include "gradient-bg.hpp"
#include <iostream>
#include <algorithm>

const char* GradientBgEffect::get_name() const { return "Gradient Bg"; }

std::vector<EffectParameter> GradientBgEffect::get_parameters() const {
    std::vector<EffectParameter> params = {
        {"blend_power", "Color blend power (1.0 - hard, 3.0+ - liquid)", blend_power},
        {"bg_color", "Base background color (void)", bg_color},
        {"enable_stripes", "Enable topographic lines", enable_stripes},
        {"stripes_density", "Contour line density", stripes_density},
        {"stripes_opacity", "Contour line opacity", stripes_opacity},
        {"dithering_amount", "Anti-banding noise (color stepping)", dithering_amount},
        {"point_count", "Number of active points (0-16)", active_points}
    };
    register_standard_params(params);
    return params;
}

void GradientBgEffect::set_parameter(const std::string& name, const EffectParameterValue& value) {
    if (apply_standard_param(name, value)) return;

    try {
        if (name == "blend_power") blend_power = std::get<float>(value);
        else if (name == "bg_color") bg_color = std::get<glm::vec3>(value);
        else if (name == "enable_stripes") enable_stripes = std::get<bool>(value);
        else if (name == "stripes_density") stripes_density = std::get<float>(value);
        else if (name == "stripes_opacity") stripes_opacity = std::get<float>(value);
        else if (name == "dithering_amount") dithering_amount = std::get<float>(value);
        else if (name == "point_count") active_points = std::clamp(std::get<int>(value), 0, 16);
    } catch (const std::bad_variant_access& e) {
        std::cerr << "[" << get_name() << "] Warning: Type mismatch for '" << name << "'\n";
    }
}

bool GradientBgEffect::initialize(ICoreContext* core, uint32_t width, uint32_t height) {
    // 1. Connect to the memory bus
    p_positions = core->get_blackboard()->bind_float_array("grad.positions", 32); 
    p_colors    = core->get_blackboard()->bind_float_array("grad.colors", 48);    
    p_radii     = core->get_blackboard()->bind_float_array("grad.radii", 16);     

    // 2. Initialize the standard Wayland rendering pipeline
    bool success = init_standard_pipeline(core, "gradient_vert.glsl", "gradient_frag.glsl");
    if (success) {
        std::cout << "\033[32m[" << get_name() << "] Standard pipeline initialized.\033[0m\n";
    }
    return success;
}

void GradientBgEffect::bind_custom_uniforms() {
    GLint prog;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prog);

    // Array uniforms use the [0] index to identify the base pointer
    glUniform1i(glGetUniformLocation(prog, "active_points"), active_points);
    if (active_points > 0) {
        if (p_positions) glUniform2fv(glGetUniformLocation(prog, "point_positions[0]"), active_points, p_positions);
        if (p_colors)    glUniform3fv(glGetUniformLocation(prog, "point_colors[0]"), active_points, p_colors);
        if (p_radii)     glUniform1fv(glGetUniformLocation(prog, "point_radii[0]"), active_points, p_radii);
    }
    
    glUniform1f(glGetUniformLocation(prog, "blend_power"), blend_power);
    glUniform3fv(glGetUniformLocation(prog, "bg_color"), 1, &bg_color[0]);
    glUniform1i(glGetUniformLocation(prog, "enable_stripes"), enable_stripes ? 1 : 0);
    glUniform1f(glGetUniformLocation(prog, "stripes_density"), stripes_density);
    glUniform1f(glGetUniformLocation(prog, "stripes_opacity"), stripes_opacity);
    glUniform1f(glGetUniformLocation(prog, "dithering_amount"), dithering_amount);
}

extern "C" {
    uint32_t get_abi_version() { return SHADER_DESK_ABI_VERSION; }
    IWallpaperEffectABI* create_effect() { return new GradientBgEffect(); }
    void destroy_effect(IWallpaperEffectABI* effect) { delete static_cast<WallpaperEffect*>(effect); }
}