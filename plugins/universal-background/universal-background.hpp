#pragma once
#include "wallpaper-effect.hpp"
#include <GLES3/gl3.h>
#include <glm/glm.hpp>

class UniversalBackground : public WallpaperEffect {
public:
    UniversalBackground() = default;
    ~UniversalBackground() override { cleanup(); }

    const char* get_name() const override { return "Universal Background"; }
    bool initialize(ICoreContext* core, uint32_t width, uint32_t height) override;
    void render(uint32_t width, uint32_t height, float dt) override; 

    void cleanup() override;

    std::vector<EffectParameter> get_parameters() const override;
    void set_parameter(const std::string& name, const EffectParameterValue& value) override;

private:
    GLuint program = 0, vao = 0;
    float time = 0.0f;
    
    glm::vec3 color_top = {0.05f, 0.08f, 0.15f};
    glm::vec3 color_bottom = {0.01f, 0.02f, 0.05f};
    int gradient_type = 2; // Radial by default
    float pulse_speed = 0.5f;

    GLuint u_color_top = 0, u_color_bottom = 0, u_gradient_type = 0, u_time = 0, u_pulse_speed = 0;
};