// Src/plugins/dither-effect/dither-effect.hpp
#ifndef DITHER_EFFECT_HPP
#define DITHER_EFFECT_HPP

#include "wallpaper-effect.hpp"
#include <GLES3/gl3.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>

// ==============================================================================
// DITHER EFFECT PLUGIN
// Applies retro pixelation, color banding limitation, and bayer matrix dithering.
// 
// ARCHITECTURE NOTE: 
// This plugin utilizes the "Standard Render Pipeline" provided by the WallpaperEffect 
// base class. It does NOT manually manage OpenGL programs, VAOs, delta times, 
// or implement the render() and cleanup() methods. 
// Instead, it relies on init_standard_pipeline() and bind_custom_uniforms().
// ==============================================================================
class DitherEffectEffect : public WallpaperEffect {
public:
    DitherEffectEffect() = default;
    
    // We don't need a custom destructor. 
    // The base class automatically cleans up the shaders, FBOs, and VAOs.
    ~DitherEffectEffect() override = default;

    // --- Core ABI Implementation ---
    const char* get_name() const override;
    
    std::vector<EffectParameter> get_parameters() const override;
    void set_parameter(const std::string& name, const EffectParameterValue& value) override;

    // Initializes the standard pipeline and triggers shader compilation.
    bool initialize(ICoreContext* core, uint32_t width, uint32_t height) override;

protected:
    // ==========================================================================
    // STANDARD PIPELINE HOOK (Inversion of Control)
    // ==========================================================================
    // This method is automatically called by the base class inside the render() loop
    // right after glUseProgram() and right before glDrawArrays().
    // We use it exclusively to dispatch our custom shader uniforms.
    void bind_custom_uniforms() override;

private:
    // --- Shader Theme Management ---
    std::string active_shader = "dither";

    // --- Visual Parameters (Configurable via Lua) ---
    float dither_spread = 0.5f;     // Noise intensity (0.0 = clean banding, 1.0 = heavy noise)
    float downsample_scale = 3.0f;  // Virtual pixel size (1.0 = native, 4.0 = retro blocky)
    int bayer_size = 1;             // Matrix resolution: 0 = 2x2, 1 = 4x4, 2 = 8x8
    int colors_count = 4;           // Number of active colors in the palette (2 to 16)
};

#endif // DITHER_EFFECT_HPP