#include "image-bg.hpp"
#include <iostream>
#include <algorithm>
#include <unordered_map>

// Lightweight image loading library. Must be present in the plugin directory.
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// ==============================================================================
// SHARED TEXTURE POOL
// Allows multiple physical monitors to share the exact same OpenGL Texture ID 
// if they are displaying the same image at the same resolution. 
// Saves massive amounts of VRAM in multi-monitor setups.
// ==============================================================================
struct SharedTexture {
    GLuint id = 0;
    float w = 1.0f;
    float h = 1.0f;
    int res = 0;
    int ref_count = 0;
};
static std::unordered_map<std::string, SharedTexture> s_texture_pool;


ImageBgEffect::ImageBgEffect() = default;

ImageBgEffect::~ImageBgEffect() {
    // 1. Signal the worker thread to abort expensive CPU downscaling
    abort_load.store(true);
    
    // 2. Block until the thread gracefully exits
    if (worker_thread.joinable()) {
        worker_thread.join();
    }
    
    // 3. Decrement reference count in the Shared Texture Pool
    on_cleanup(); 
}

const char* ImageBgEffect::get_name() const { 
    return "Image Bg"; 
}

std::vector<EffectParameter> ImageBgEffect::get_parameters() const {
    std::vector<EffectParameter> params = {
        {"fill_mode", "0: Cover, 1: Contain, 2: Stretch, 3: Tile", fill_mode},
        {"scale", "Image scale (Zoom)", scale},
        {"offset", "UV Offset (Pan X/Y)", offset},
        {"rotation", "Rotation in degrees", rotation},
        {"brightness", "Brightness multiplier", brightness},
        {"contrast", "Contrast adjustment", contrast},
        {"saturation", "Color saturation (0.0 = Grayscale)", saturation},
        {"tint_color", "Tint color (RGB)", tint_color},
        {"tint_intensity", "Tint blending intensity (0.0 - 1.0)", tint_intensity},
        {"blur_radius", "Gaussian blur intensity", blur_radius},
        {"base_image_path", "Main background image", base_image_path},
        {"optimize_vram", "Use 16-bit texture formats to halve VRAM usage", optimize_vram},
        {"dynamic_lod", "Auto-scale VRAM resolution based on Lua scale", dynamic_lod},
        {"max_texture_size", "Absolute hardware limit for textures", max_texture_size}
    };
    
    // CRITICAL: Injects 'layer_fbo_scale' support seamlessly for Lua Control Plane
    register_standard_params(params);
    return params;
}

void ImageBgEffect::set_parameter(const std::string& name, const EffectParameterValue& value) {
    // Intercept standard pipeline parameters (e.g., layer_fbo_scale)
    if (apply_standard_param(name, value)) return;

    try {
        if (name == "fill_mode") fill_mode = std::get<int>(value);
        else if (name == "scale") scale = std::get<float>(value);
        else if (name == "offset") offset = std::get<glm::vec2>(value);
        else if (name == "rotation") rotation = std::get<float>(value);
        else if (name == "brightness") brightness = std::get<float>(value);
        else if (name == "contrast") contrast = std::get<float>(value);
        else if (name == "saturation") saturation = std::get<float>(value);
        else if (name == "tint_color") tint_color = std::get<glm::vec3>(value);
        else if (name == "tint_intensity") tint_intensity = std::get<float>(value);
        else if (name == "blur_radius") blur_radius = std::get<float>(value);
        else if (name == "optimize_vram") optimize_vram = std::get<bool>(value);
        else if (name == "dynamic_lod") dynamic_lod = std::get<bool>(value);
        else if (name == "max_texture_size") max_texture_size = std::get<int>(value);
        else if (name == "base_image_path") {
            std::string new_path = std::get<std::string>(value);
            if (new_path != base_image_path) {
                base_image_path = new_path;
                base_image_pending = true; // Trigger the state machine
            }
        }
    } catch (const std::bad_variant_access& e) {
        std::cerr << "[" << get_name() << "] Warning: Type mismatch for parameter '" << name << "'\n";
    }
}

bool ImageBgEffect::initialize(ICoreContext* core, uint32_t width, uint32_t height) {
    // Rely entirely on the SDK's Standard Pipeline to compile shaders and create the VAO
    bool success = init_standard_pipeline(core, "image_vert.glsl", "image_frag.glsl");
    if (success) {
        std::cout << "\033[32m[" << get_name() << "] Standard pipeline initialized successfully.\033[0m\n";
    }
    return success;
}

// ==============================================================================
// RENDER HOOK (VRAM PRE-PASS)
// We override the base render() method to execute our texture state machine 
// BEFORE the base class binds FBOs and calls glUseProgram. 
// This prevents OpenGL state corruption.
// ==============================================================================
void ImageBgEffect::render(uint32_t width, uint32_t height, float dt) {
    // 1. Calculate actual physical pixels required based on FBO scale.
    // This prevents loading an 8K texture into VRAM when the layer is scaled down to 720p.
    uint32_t target_w = std::max<uint32_t>(1, static_cast<uint32_t>(width * layer_fbo_scale));
    uint32_t target_h = std::max<uint32_t>(1, static_cast<uint32_t>(height * layer_fbo_scale));

    // 2. Process async queues. Uploads to VRAM if the worker thread finished.
    process_textures(target_w, target_h);

    // 3. Delegate drawing to the standard Pipeline
    WallpaperEffect::render(width, height, dt);
}

// ==============================================================================
// UNIFORMS DISPATCH (Inversion of Control)
// Called by the base class immediately before glDrawArrays.
// ==============================================================================
void ImageBgEffect::bind_custom_uniforms() {
    if (!base_image_id) return; // Prevent black screen artifact if loading is still pending

    GLint prog;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prog);

    // Note: glGetUniformLocation is safe and fast here. Modern OpenGL drivers 
    // cache locations in an O(1) String Hash Map internally.
    glUniform1i(glGetUniformLocation(prog, "fill_mode"), fill_mode);
    glUniform1f(glGetUniformLocation(prog, "scale"), scale);
    glUniform2fv(glGetUniformLocation(prog, "offset"), 1, &offset[0]);
    glUniform1f(glGetUniformLocation(prog, "rotation"), rotation);
    glUniform1f(glGetUniformLocation(prog, "brightness"), brightness);
    glUniform1f(glGetUniformLocation(prog, "contrast"), contrast);
    glUniform1f(glGetUniformLocation(prog, "saturation"), saturation);
    glUniform3fv(glGetUniformLocation(prog, "tint_color"), 1, &tint_color[0]);
    glUniform1f(glGetUniformLocation(prog, "tint_intensity"), tint_intensity);
    glUniform1f(glGetUniformLocation(prog, "blur_radius"), blur_radius);

    // Bind specific texture data
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, base_image_id);
    glUniform1i(glGetUniformLocation(prog, "base_image"), 0);
    glUniform2f(glGetUniformLocation(prog, "base_image_resolution"), base_image_w, base_image_h);
}

// ==============================================================================
// CPU BILINEAR DOWNSAMPLER
// Safely resizes massive images in RAM before they are sent to VRAM.
// Uses RAII vector to prevent memory leaks if the thread is aborted.
// ==============================================================================
static std::vector<uint8_t> cpu_downscale_bilinear(unsigned char* stbi_data, int& w, int& h, int channels, int max_dim) {
    if (w <= max_dim && h <= max_dim) {
        // Fast Path: No downscaling needed.
        std::vector<uint8_t> res(stbi_data, stbi_data + (w * h * channels));
        stbi_image_free(stbi_data);
        return res;
    }

    float ratio = std::min((float)max_dim / w, (float)max_dim / h);
    int nw = static_cast<int>(w * ratio);
    int nh = static_cast<int>(h * ratio);

    std::vector<uint8_t> ndata(nw * nh * channels);

    for (int y = 0; y < nh; ++y) {
        for (int x = 0; x < nw; ++x) {
            float gx = x / ratio;
            float gy = y / ratio;
            int gxi = static_cast<int>(gx);
            int gyi = static_cast<int>(gy);
            
            float tx = gx - gxi;
            float ty = gy - gyi;
            float c00 = (1 - tx) * (1 - ty);
            float c10 = tx * (1 - ty);
            float c01 = (1 - tx) * ty;
            float c11 = tx * ty;

            for (int c = 0; c < channels; ++c) {
                int p00 = stbi_data[(gyi * w + gxi) * channels + c];
                int p10 = (gxi + 1 < w) ? stbi_data[(gyi * w + gxi + 1) * channels + c] : p00;
                int p01 = (gyi + 1 < h) ? stbi_data[((gyi + 1) * w + gxi) * channels + c] : p00;
                int p11 = ((gxi + 1 < w) && (gyi + 1 < h)) ? stbi_data[((gyi + 1) * w + gxi + 1) * channels + c] : p00;

                float val = p00 * c00 + p10 * c10 + p01 * c01 + p11 * c11;
                ndata[(y * nw + x) * channels + c] = static_cast<uint8_t>(std::min(std::max(val, 0.0f), 255.0f));
            }
        }
    }
    
    stbi_image_free(stbi_data);
    w = nw;
    h = nh;
    return ndata;
}

// ==============================================================================
// ASYNCHRONOUS WORKER
// ==============================================================================
void ImageBgEffect::start_async_load(const std::string& path, int target_res) {
    if (is_loading.load()) {
        abort_load.store(true); // Signal current thread to die quickly
    }
    
    if (worker_thread.joinable()) {
        worker_thread.join();
    }

    is_loading.store(true);
    abort_load.store(false);

    worker_thread = std::thread([this, path, target_res]() {
        // We do NOT flip vertically here. The `image_vert.glsl` handles it perfectly via UVs.
        stbi_set_flip_vertically_on_load(false); 
        
        int w, h, c;
        unsigned char* d = stbi_load(path.c_str(), &w, &h, &c, 0); 
        
        if (abort_load.load()) {
            if (d) stbi_image_free(d);
            is_loading.store(false);
            return;
        }

        if (d) {
            // Converts to a safe RAII vector
            std::vector<uint8_t> processed_data = cpu_downscale_bilinear(d, w, h, c, target_res);
            
            if (!abort_load.load()) {
                std::lock_guard<std::mutex> lock(async_mutex);
                async_data = std::move(processed_data); // Zero-copy move
                async_w = w;
                async_h = h;
                async_c = c;
                async_path = path;
                async_target_res = target_res;
            }
        } else {
            std::cerr << "[" << get_name() << "] Async load failed for: " << path << std::endl;
        }

        is_loading.store(false);
    });
}

// ==============================================================================
// MAIN THREAD SYNC POINT (VRAM UPLOAD)
// ==============================================================================
void ImageBgEffect::check_async_results() {
    std::lock_guard<std::mutex> lock(async_mutex);
    if (async_data.empty()) return; 

    std::string new_key = async_path + "_" + std::to_string(async_target_res);
    release_current_texture(); // Drop reference to old texture

    glGenTextures(1, &base_image_id);
    glBindTexture(GL_TEXTURE_2D, base_image_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    GLenum format = (async_c == 3) ? GL_RGB : GL_RGBA;
    GLenum internal_format = (async_c == 3) ? GL_RGB8 : GL_RGBA8;
    
    if (optimize_vram) {
        // VRAM Optimization: Compresses textures internally on the GPU.
        // Invisible to the naked eye for backgrounds, but saves 50% VRAM bandwidth.
        internal_format = (async_c == 3) ? GL_RGB565 : GL_RGBA4;
    }

    // Safely upload the byte array. PixelStorei is required for 3-channel (RGB) alignment.
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1); 
    glTexImage2D(GL_TEXTURE_2D, 0, internal_format, async_w, async_h, 0, format, GL_UNSIGNED_BYTE, async_data.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    base_image_w = static_cast<float>(async_w);
    base_image_h = static_cast<float>(async_h);
    current_vram_res = async_target_res;
    loaded_texture_key = new_key;

    // Register into the global pool so other monitors can use it immediately without loading
    s_texture_pool[new_key] = {base_image_id, base_image_w, base_image_h, current_vram_res, 1};
    
    // Clear RAM buffer
    async_data.clear();
    async_data.shrink_to_fit();
}

// ==============================================================================
// TEXTURE STATE MACHINE
// ==============================================================================
void ImageBgEffect::process_textures(uint32_t screen_w, uint32_t screen_h) {
    if (!m_core_ptr || base_image_path.empty()) return;

    int required_res = max_texture_size; 
    
    if (dynamic_lod) {
        // Determine physical pixel requirement
        float needed_pixels = std::max(screen_w, screen_h);
        if (fill_mode == 1) needed_pixels *= scale; // Scale only matters in CONTAIN mode
        
        // Select appropriate MipMap tier
        int lod_tiers[] = {256, 512, 1024, 2048, 4096, 8192, 16384};
        for (int tier : lod_tiers) {
            if (tier >= needed_pixels) {
                required_res = tier;
                break;
            }
        }
    }
    required_res = std::min(required_res, max_texture_size);

    // Resolve path relative to the plugin's isolated bundle directory
    std::string fp = base_image_path;
    if (fp[0] != '/') {
        const char* bundle_path = m_core_ptr->get_bundle_path(get_name());
        if (bundle_path && bundle_path[0] != '\0') fp = std::string(bundle_path) + "/" + fp;
    }

    std::string target_key = fp + "_" + std::to_string(required_res);

    if (base_image_pending || (target_key != loaded_texture_key)) {
        
        // Check if another monitor already loaded this exact image & resolution
        auto it = s_texture_pool.find(target_key);
        if (it != s_texture_pool.end()) {
            release_current_texture();
            it->second.ref_count++;
            
            base_image_id = it->second.id;
            base_image_w = it->second.w;
            base_image_h = it->second.h;
            current_vram_res = it->second.res;
            loaded_texture_key = target_key;
            
            base_image_pending = false;
            return;
        }

        // Trigger background worker if not in cache
        if (!is_loading.load()) {
            start_async_load(fp, required_res);
            base_image_pending = false;
        }
    }

    // Pull data from thread if ready
    check_async_results();
}

void ImageBgEffect::release_current_texture() {
    if (!loaded_texture_key.empty()) {
        auto it = s_texture_pool.find(loaded_texture_key);
        if (it != s_texture_pool.end()) {
            it->second.ref_count--;
            // Only destroy the OpenGL texture if no other monitor is using it
            if (it->second.ref_count <= 0) {
                glDeleteTextures(1, &it->second.id);
                s_texture_pool.erase(it);
            }
        }
        loaded_texture_key = "";
        base_image_id = 0;
        current_vram_res = 0;
    }
}

void ImageBgEffect::on_cleanup() {
    release_current_texture(); 
}

extern "C" {
    uint32_t get_abi_version() { return SHADER_DESK_ABI_VERSION; }
    IWallpaperEffectABI* create_effect() { return new ImageBgEffect(); }
    void destroy_effect(IWallpaperEffectABI* effect) { delete static_cast<WallpaperEffect*>(effect); }
}