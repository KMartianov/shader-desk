#ifndef IMAGE_BG_HPP
#define IMAGE_BG_HPP

#include <shader-desk/wallpaper-effect.hpp>
#include <GLES3/gl3.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>

// ==============================================================================
// IMAGE BACKGROUND EFFECT
// Features: 
// 1. Asynchronous I/O loading (Zero-Latency for the Wayland epoll loop).
// 2. CPU-bound Bilinear Downscaling (Dynamic LOD).
// 3. Shared VRAM Texture Pool (Instancing the same image across multiple monitors).
// 4. Integrates seamlessly with the Standard Render Pipeline (FBO scaling support).
// ==============================================================================
class ImageBgEffect : public WallpaperEffect {
public:
    ImageBgEffect();
    ~ImageBgEffect() override;

    // --- Core ABI Implementation ---
    const char* get_name() const override;
    std::vector<EffectParameter> get_parameters() const override;
    void set_parameter(const std::string& name, const EffectParameterValue& value) override;

    bool initialize(ICoreContext* core, uint32_t width, uint32_t height) override;
    
    // Explicit override to process VRAM transactions safely outside the draw hook
    void render(uint32_t width, uint32_t height, float dt) override;
    
    // Safely unbinds from the Shared Texture Pool
    void on_cleanup() override;

protected:
    // Dispatches custom parameters to the Standard Pipeline just before glDrawArrays
    void bind_custom_uniforms() override;

private:
    // --- Visual Parameters (Lua Configurable) ---
    int fill_mode = 0;              // 0: Cover, 1: Contain, 2: Stretch, 3: Tile
    float scale = 1.0f;             // Image UV zoom
    glm::vec2 offset = {0.0f, 0.0f}; // UV Pan X/Y
    float rotation = 0.0f;          // Rotation in degrees
    
    // --- Color Correction ---
    float brightness = 1.0f;
    float contrast = 1.0f;
    float saturation = 1.0f;
    glm::vec3 tint_color = {0.0f, 0.0f, 0.0f};
    float tint_intensity = 0.0f;
    float blur_radius = 0.0f;       // Hardware MipMap based fast blur
    
    // --- Optimization Settings ---
    bool optimize_vram = true;      // Compresses 8-bit RGB to 16-bit RGB565 in VRAM
    bool dynamic_lod = true;        // Auto-scales RAM/VRAM usage based on screen scale
    int max_texture_size = 8192;    // Absolute hardware constraint

    // --- Texture State Variables ---
    GLuint base_image_id = 0;
    std::string base_image_path = "";
    std::string loaded_texture_key = ""; // Format: "path_resolution" (e.g., "bg.png_2048")
    bool base_image_pending = false;
    float base_image_w = 1.0f;
    float base_image_h = 1.0f;
    int current_vram_res = 0;

    // --- Asynchronous Loading Architecture ---
    std::atomic<bool> is_loading{false};
    std::atomic<bool> abort_load{false};
    std::mutex async_mutex;
    std::thread worker_thread;
    
    // RAII buffer for safely passing pixel data across thread boundaries
    std::vector<uint8_t> async_data;
    int async_w = 0, async_h = 0, async_c = 0;
    std::string async_path = "";
    int async_target_res = 0;

    // --- Internal Methods ---
    void process_textures(uint32_t screen_w, uint32_t screen_h);
    void release_current_texture();
    void start_async_load(const std::string& path, int target_res);
    void check_async_results();
};

#endif // IMAGE_BG_HPP