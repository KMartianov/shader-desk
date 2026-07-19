#ifndef SOLID_BG_HPP
#define SOLID_BG_HPP

#include <shader-desk/wallpaper-effect.hpp>
#include <GLES3/gl3.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>

class SolidBgEffect : public WallpaperEffect {
public:
    SolidBgEffect() = default;
    ~SolidBgEffect() override = default;

    const char* get_name() const override;
    std::vector<EffectParameter> get_parameters() const override;
    void set_parameter(const std::string& name, const EffectParameterValue& value) override;
    bool initialize(ICoreContext* core, uint32_t width, uint32_t height) override;

protected:
    // Hooks into the Standard Pipeline right before glDrawArrays
    void bind_custom_uniforms() override;

private:
    int gradient_type = 3;
    int color_space = 1;
    glm::vec3 color_1 = glm::vec3(0.89f, 0.28f, 0.20f);
    glm::vec3 color_2 = glm::vec3(0.15f, 0.40f, 0.85f);
    glm::vec3 color_3 = glm::vec3(0.95f, 0.75f, 0.20f);
    glm::vec3 color_4 = glm::vec3(0.60f, 0.20f, 0.80f);
    float angle = 45.0f;
    glm::vec2 radial_center = glm::vec2(0.5f, 0.5f);
    float radial_radius = 1.2f;
    float dither_strength = 1.0f;
};

#endif // SOLID_BG_HPP