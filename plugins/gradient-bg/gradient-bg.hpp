#ifndef GRADIENT_BG_HPP
#define GRADIENT_BG_HPP

#include <shader-desk/wallpaper-effect.hpp>
#include <GLES3/gl3.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>

class GradientBgEffect : public WallpaperEffect {
public:
    GradientBgEffect() = default;
    ~GradientBgEffect() override = default;

    const char* get_name() const override;
    std::vector<EffectParameter> get_parameters() const override;
    void set_parameter(const std::string& name, const EffectParameterValue& value) override;
    bool initialize(ICoreContext* core, uint32_t width, uint32_t height) override;

protected:
    void bind_custom_uniforms() override;

private:
    float blend_power = 2.5f;
    glm::vec3 bg_color = glm::vec3(0.05f, 0.05f, 0.08f);
    bool enable_stripes = false;
    float stripes_density = 15.0f;
    float stripes_opacity = 0.15f;
    float dithering_amount = 0.02f;
    int active_points = 0; 

    // --- BlackBoard Pointers (Zero-Latency IPC) ---
    float* p_positions = nullptr;
    float* p_colors = nullptr;
    float* p_radii = nullptr;
};

#endif // GRADIENT_BG_HPP