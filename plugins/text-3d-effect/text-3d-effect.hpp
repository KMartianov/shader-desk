// Src/plugins/text-3d-effect/text-3d-effect.hpp
#pragma once

#include "wallpaper-effect.hpp"
#include <GLES3/gl3.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>

#include <thread>
#include <atomic>
#include <mutex>

class Text3DEffect : public WallpaperEffect {
public:
    Text3DEffect();
    ~Text3DEffect() override;

    const char* get_name() const override { return "3D Raymarched Text"; }
    std::vector<EffectParameter> get_parameters() const override;
    void set_parameter(const std::string& name, const EffectParameterValue& value) override;

    bool initialize(ICoreContext* core, uint32_t width, uint32_t height) override;
    void render(uint32_t width, uint32_t height, float dt) override;
    void cleanup() override;

private:
    void load_font();
    void start_async_sdf_generation(const std::string& target_text);
    void upload_texture_if_ready();
    void update_rotation(float dt);

    // --- OpenGL Objects ---
    GLuint program = 0;
    GLuint vao = 0; 
    GLuint sdf_texture = 0;
    
    // --- Uniform locations ---
    GLuint u_resolution = 0, u_time = 0;
    GLuint u_inv_model = 0;
    GLuint u_sdf_multiplier = 0;
    GLuint u_extrusion = 0, u_bend_radius = 0;
    GLuint u_text_color = 0, u_bg_color = 0;
    GLuint u_text_aspect = 0;

    // --- Settings from Lua (Config) ---
    std::string text_string = "WAYLAND";
    std::string font_path = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf";
    float extrusion_depth = 0.15f;
    float bend_radius = 2.0f; 
    glm::vec3 text_color = {0.0f, 1.0f, 0.7f};
    glm::vec3 bg_color = {0.05f, 0.05f, 0.08f};
    
    // NEW ROTATION PHYSICS AND PIVOT PARAMETERS
    glm::vec3 base_rotation_axis = {0.0f, 1.0f, 0.0f}; // Ideal rotation axis (Y by default)
    float base_rotation_speed = 0.5f;                  // Rotation speed around the ideal axis
    float mouse_sensitivity = 3.0f;                    // Mouse reaction strength
    float return_friction = 3.0f;                      // Spring return speed (0 = never return)
    glm::vec3 pivot_offset = {0.0f, 0.0f, 0.0f};       // Manual center of mass setup (X, Y, Z)

    // --- Animation and text state ---
    float time = 0.0f;
    std::string active_text = "";
    float text_aspect_ratio = 1.0f;
    float sdf_multiplier = 1.0f;

    // Split rotation state
    float base_angle = 0.0f;
    glm::quat mouse_quat = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    glm::quat final_orientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    
    // --- BlackBoard Pointers ---
    float* p_accum_x = nullptr;
    float* p_accum_y = nullptr;
    float last_mouse_x = 0.0f;
    float last_mouse_y = 0.0f;
    char* p_dynamic_text = nullptr;

    // --- ASYNCHRONOUS GENERATION ---
    std::vector<unsigned char> ttf_buffer;
    bool font_loaded = false;
    std::thread worker_thread;
    std::atomic<bool> is_generating{false};
    std::atomic<bool> texture_ready{false};
    std::atomic<bool> shutdown_requested{false};
    
    std::mutex async_mutex;
    std::vector<unsigned char> next_bitmap;
    int next_width = 0;
    int next_height = 0;
    float next_aspect = 1.0f;
    float next_sdf_mult = 1.0f;
};