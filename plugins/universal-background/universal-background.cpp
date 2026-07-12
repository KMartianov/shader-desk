#include "universal-background.hpp"
#include "shader-utils.hpp"
#include <iostream>
#include <algorithm>

bool UniversalBackground::initialize(ICoreContext* core, uint32_t, uint32_t) {
    if (program) return true;
    std::string vert = shader_utils::load_shader_source(core, get_name(), "bg_vert.glsl");
    std::string frag = shader_utils::load_shader_source(core, get_name(), "bg_frag.glsl");
    if (vert.empty() || frag.empty()) return false;

    program = shader_utils::create_shader_program(vert, frag);
    if (!program) return false;

    u_color_top = glGetUniformLocation(program, "color_top");
    u_color_bottom = glGetUniformLocation(program, "color_bottom");
    u_gradient_type = glGetUniformLocation(program, "gradient_type");
    u_time = glGetUniformLocation(program, "time");
    u_pulse_speed = glGetUniformLocation(program, "pulse_speed");

    glGenVertexArrays(1, &vao);
    return true;
}

void UniversalBackground::render(uint32_t width, uint32_t height, float dt) {
    glUseProgram(program);
    glDisable(GL_DEPTH_TEST); // Background doesn't need depth testing
    
    // USE real dt instead of hardcoded 0.016f
    time += dt; 
    
    glUniform3fv(u_color_top, 1, &color_top[0]);
    glUniform3fv(u_color_bottom, 1, &color_bottom[0]);
    glUniform1i(u_gradient_type, gradient_type);
    glUniform1f(u_time, time);
    glUniform1f(u_pulse_speed, pulse_speed);

    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
}

void UniversalBackground::cleanup() {
    if (program) { glDeleteProgram(program); program = 0; }
    if (vao) { glDeleteVertexArrays(1, &vao); vao = 0; }
}

std::vector<EffectParameter> UniversalBackground::get_parameters() const {
    return {
        {"color_top", "Main/Top gradient color", color_top},
        {"color_bottom", "Bottom gradient color", color_bottom},
        {"gradient_type", "Type: 0=Solid, 1=Vertical, 2=Radial", gradient_type},
        {"pulse_speed", "Color pulse speed (0 = off)", pulse_speed}
    };
}

void UniversalBackground::set_parameter(const std::string& name, const EffectParameterValue& value) {
    try {
        if (name == "color_top") color_top = std::get<glm::vec3>(value);
        else if (name == "color_bottom") color_bottom = std::get<glm::vec3>(value);
        else if (name == "gradient_type") gradient_type = std::clamp(std::get<int>(value), 0, 2);
        else if (name == "pulse_speed") pulse_speed = std::get<float>(value);
    } catch (...) {}
}

extern "C" {
    uint32_t get_abi_version() { return SHADER_DESK_ABI_VERSION; }

    IWallpaperEffectABI* create_effect() { return new UniversalBackground(); }
    void destroy_effect(IWallpaperEffectABI* e) { delete static_cast<UniversalBackground*>(e); }
}