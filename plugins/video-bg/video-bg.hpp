#ifndef VIDEO_BG_HPP
#define VIDEO_BG_HPP

#include "wallpaper-effect.hpp"
#include <GLES3/gl3.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>

// Forward declarations for libmpv
struct mpv_handle;
struct mpv_render_context;

class VideoBgEffect : public WallpaperEffect {
public:
    VideoBgEffect();
    ~VideoBgEffect() override;

    const char* get_name() const override;
    std::vector<EffectParameter> get_parameters() const override;
    void set_parameter(const std::string& name, const EffectParameterValue& value) override;

    bool initialize(ICoreContext* core, uint32_t width, uint32_t height) override;
    void render(uint32_t width, uint32_t height, float dt) override;
    void cleanup() override;

private:
    ICoreContext* m_core = nullptr;
    
    // --- GL Resources ---
    GLuint program = 0;
    GLuint vao = 0;
    float time = 0.0f;

    // --- Video Target Texture/FBO ---
    GLuint video_fbo = 0;
    GLuint video_tex = 0;
    int video_w = 1;
    int video_h = 1;

    // --- MPV State ---
    mpv_handle* mpv = nullptr;
    mpv_render_context* mpv_gl = nullptr;
    bool video_path_pending = false;

    // --- Parameters ---
    std::string video_path = "";
    int fill_mode = 0;
    float border_radius = 0.05f; 
    float scale = 1.0f;
    glm::vec2 offset = glm::vec2(0.0f);
    float rotation = 0.0f;
    float brightness = 1.0f;
    float contrast = 1.0f;
    float saturation = 1.0f;
    glm::vec3 tint_color = glm::vec3(0.0f);
    float tint_intensity = 0.0f;
    float blur_radius = 0.0f;
    float playback_speed = 1.0f;
    float volume = 100.0f;
    bool is_muted = true;
    bool is_paused = false;
    bool debug_mpv = false; 

    // --- Uniforms ---
    GLuint u_time, u_resolution, u_fill_mode, u_border_radius, u_scale, u_offset, u_rotation;
    GLuint u_brightness, u_contrast, u_saturation, u_tint_color, u_tint_intensity;
    GLuint u_blur_radius, u_base_image;
    
    // CPU-Calculated optimization uniforms
    GLuint u_pixel_size, u_screen_aspect, u_tex_aspect;

    void reallocate_fbo(int w, int h);
};

#endif // VIDEO_BG_HPP