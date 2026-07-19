#ifndef POSTPROCESS_EFFECT_HPP
#define POSTPROCESS_EFFECT_HPP

#include <shader-desk/wallpaper-effect.hpp>
#include <GLES3/gl3.h>
#include <string>
#include <vector>

class PostprocessEffectEffect : public WallpaperEffect {
public:
    PostprocessEffectEffect() = default;
    ~PostprocessEffectEffect() override = default;

    const char* get_name() const override;
    std::vector<EffectParameter> get_parameters() const override;
    void set_parameter(const std::string& name, const EffectParameterValue& value) override;
    bool initialize(ICoreContext* core, uint32_t width, uint32_t height) override;

protected:
    void bind_custom_uniforms() override;

private:
    std::string active_shader = "postprocess";
    float intensity = 0.3f;
    float speed = 2.0f;
    float scale = 15.0f;
    int variant = 1;
};

#endif // POSTPROCESS_EFFECT_HPP