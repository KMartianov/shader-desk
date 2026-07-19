#ifndef _GNU_SOURCE
#define _GNU_SOURCE // Required for RTLD_DEFAULT
#endif

#include "video-bg.hpp"
#include "shader-utils.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <dlfcn.h>
#include <EGL/egl.h>
#include <mpv/client.h>
#include <mpv/render_gl.h>

VideoBgEffect::VideoBgEffect() = default;

VideoBgEffect::~VideoBgEffect() {
    on_cleanup();
}

const char* VideoBgEffect::get_name() const {
    return "Video Bg";
}

// ------------------------------------------------------------------------------
// PARAMETER MANAGEMENT
// ------------------------------------------------------------------------------
std::vector<EffectParameter> VideoBgEffect::get_parameters() const {
    return {
        {"video_path", "Path to video file or URL", video_path},
        {"fill_mode", "0: Cover, 1: Contain", fill_mode},
        {"border_radius", "Rounded corners (0.0 - 0.5)", border_radius},
        {"scale", "Image scale (Zoom)", scale},
        {"offset", "UV Offset (Pan X/Y)", offset},
        {"rotation", "Rotation in degrees", rotation},
        {"brightness", "Brightness multiplier", brightness},
        {"contrast", "Contrast adjustment", contrast},
        {"saturation", "Color saturation (0.0 = Grayscale)", saturation},
        {"tint_color", "Tint color (RGB)", tint_color},
        {"tint_intensity", "Tint blending intensity (0.0 - 1.0)", tint_intensity},
        {"blur_radius", "Fast Shader Blur radius", blur_radius},
        {"playback_speed", "Video playback speed multiplier", playback_speed},
        {"volume", "Audio volume (0.0 - 100.0)", volume},
        {"is_muted", "Mute video audio", is_muted},
        {"is_paused", "Pause playback", is_paused},
        {"debug_mpv", "Enable verbose MPV console output", debug_mpv}
    };
}

void VideoBgEffect::set_parameter(const std::string& name, const EffectParameterValue& value) {
    try {
        if (name == "video_path") {
            std::string new_path = std::get<std::string>(value);
            if (new_path != video_path) {
                video_path = new_path;
                video_path_pending = true;
            }
        }
        else if (name == "fill_mode") fill_mode = std::get<int>(value);
        else if (name == "border_radius") border_radius = std::get<float>(value);
        else if (name == "scale") scale = std::get<float>(value);
        else if (name == "offset") offset = std::get<glm::vec2>(value);
        else if (name == "rotation") rotation = std::get<float>(value);
        else if (name == "brightness") brightness = std::get<float>(value);
        else if (name == "contrast") contrast = std::get<float>(value);
        else if (name == "saturation") saturation = std::get<float>(value);
        else if (name == "tint_color") tint_color = std::get<glm::vec3>(value);
        else if (name == "tint_intensity") tint_intensity = std::get<float>(value);
        else if (name == "blur_radius") blur_radius = std::get<float>(value);
        else if (name == "playback_speed") {
            playback_speed = std::get<float>(value);
            if (mpv) {
                double d_speed = static_cast<double>(playback_speed);
                mpv_set_property_async(mpv, 0, "speed", MPV_FORMAT_DOUBLE, &d_speed);
            }
        }
        else if (name == "volume") {
            volume = std::get<float>(value);
            if (mpv) {
                double d_vol = static_cast<double>(volume);
                mpv_set_property_async(mpv, 0, "volume", MPV_FORMAT_DOUBLE, &d_vol);
            }
        }
        else if (name == "is_muted") {
            is_muted = std::get<bool>(value);
            if (mpv) {
                int mute_flag = is_muted ? 1 : 0;
                mpv_set_property_async(mpv, 0, "mute", MPV_FORMAT_FLAG, &mute_flag);
            }
        }
        else if (name == "is_paused") {
            is_paused = std::get<bool>(value);
            if (mpv) {
                int pause_flag = is_paused ? 1 : 0;
                mpv_set_property_async(mpv, 0, "pause", MPV_FORMAT_FLAG, &pause_flag);
            }
        }
        else if (name == "debug_mpv") {
            debug_mpv = std::get<bool>(value);
            if (mpv) {
                mpv_request_log_messages(mpv, debug_mpv ? "debug" : "error");
            }
        }
    } catch (const std::bad_variant_access& e) {
        std::cerr << "[" << get_name() << "] Warning: Type mismatch for parameter '" << name << "'." << std::endl;
    }
}

// ------------------------------------------------------------------------------
// INITIALIZATION & LIBMPV SETUP
// ------------------------------------------------------------------------------
bool VideoBgEffect::initialize(ICoreContext* core, uint32_t width, uint32_t height) {
    m_core = core;

    std::string vert_src = shader_utils::load_shader_source(core, get_name(), "video_vert.glsl");
    std::string frag_src = shader_utils::load_shader_source(core, get_name(), "video_frag.glsl");
    
    if (vert_src.empty() || frag_src.empty()) return false;

    program = shader_utils::create_shader_program(vert_src, frag_src);
    if (!program) return false;
    
    u_time = glGetUniformLocation(program, "time");
    u_resolution = glGetUniformLocation(program, "resolution");
    u_fill_mode = glGetUniformLocation(program, "fill_mode");
    u_border_radius = glGetUniformLocation(program, "border_radius");
    u_scale = glGetUniformLocation(program, "scale");
    u_offset = glGetUniformLocation(program, "offset");
    u_rotation = glGetUniformLocation(program, "rotation");
    u_brightness = glGetUniformLocation(program, "brightness");
    u_contrast = glGetUniformLocation(program, "contrast");
    u_saturation = glGetUniformLocation(program, "saturation");
    u_tint_color = glGetUniformLocation(program, "tint_color");
    u_tint_intensity = glGetUniformLocation(program, "tint_intensity");
    u_blur_radius = glGetUniformLocation(program, "blur_radius");
    u_base_image = glGetUniformLocation(program, "base_image");
    
    u_pixel_size = glGetUniformLocation(program, "pixel_size");
    u_screen_aspect = glGetUniformLocation(program, "screen_aspect");
    u_tex_aspect = glGetUniformLocation(program, "tex_aspect");

    glGenVertexArrays(1, &vao);

    mpv = mpv_create();
    if (!mpv) {
        std::cerr << "[" << get_name() << "] CRITICAL: Failed to create MPV instance!" << std::endl;
        return false;
    }

    mpv_request_log_messages(mpv, debug_mpv ? "debug" : "error");

    // GAPLESS LOOP PROFILE (Zero-Teardown)
    mpv_set_option_string(mpv, "vo", "libmpv");       
    mpv_set_option_string(mpv, "osc", "no");          
    mpv_set_option_string(mpv, "input-default-bindings", "no"); 
    mpv_set_option_string(mpv, "vid", "1");
    mpv_set_option_string(mpv, "ytdl", "yes");
    mpv_set_option_string(mpv, "ytdl-format", "bestvideo[height<=1080]+bestaudio/best[height<=1080]");

    mpv_set_option_string(mpv, "loop-file", "inf");
    mpv_set_option_string(mpv, "hr-seek", "yes");
    mpv_set_option_string(mpv, "keep-open", "yes");

    mpv_set_option_string(mpv, "audio", "no");
    mpv_set_option_string(mpv, "aid", "no");

    mpv_set_option_string(mpv, "hwdec", "auto-safe"); 
    mpv_set_option_string(mpv, "profile", "fast");
    mpv_set_option_string(mpv, "scale", "bilinear");
    mpv_set_option_string(mpv, "cscale", "bilinear");
    mpv_set_option_string(mpv, "dscale", "bilinear");
    mpv_set_option_string(mpv, "dither-depth", "no");
    mpv_set_option_string(mpv, "correct-downscaling", "no");
    mpv_set_option_string(mpv, "linear-downscaling", "no");
    mpv_set_option_string(mpv, "sigmoid-upscaling", "no");
    mpv_set_option_string(mpv, "deband", "no");

    if (mpv_initialize(mpv) < 0) {
        std::cerr << "[" << get_name() << "] CRITICAL: MPV initialization failed!" << std::endl;
        return false;
    }

    double d_speed = static_cast<double>(playback_speed);
    mpv_set_property_async(mpv, 0, "speed", MPV_FORMAT_DOUBLE, &d_speed);
    double d_vol = static_cast<double>(volume);
    mpv_set_property_async(mpv, 0, "volume", MPV_FORMAT_DOUBLE, &d_vol);
    int mute_flag = is_muted ? 1 : 0;
    mpv_set_property_async(mpv, 0, "mute", MPV_FORMAT_FLAG, &mute_flag);
    int pause_flag = is_paused ? 1 : 0;
    mpv_set_property_async(mpv, 0, "pause", MPV_FORMAT_FLAG, &pause_flag);

    mpv_opengl_init_params gl_init_params{
        [](void*, const char* name) -> void* {
            void* addr = (void*)eglGetProcAddress(name);
            if (!addr) addr = dlsym(RTLD_DEFAULT, name);
            return addr;
        },
        nullptr
    };

    mpv_render_param render_params[] = {
        {MPV_RENDER_PARAM_API_TYPE, (void*)MPV_RENDER_API_TYPE_OPENGL},
        {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &gl_init_params},
        // АРХИТЕКТУРНЫЙ ФИКС: Строго запрещаем MPV обращаться к Wayland (убран MPV_RENDER_PARAM_WL_DISPLAY).
        // Это ликвидирует Deadlock главного цикла ядра (epoll) при вызове mpv_terminate_destroy.
        {MPV_RENDER_PARAM_INVALID, nullptr}
    };

    if (mpv_render_context_create(&mpv_gl, mpv, render_params) < 0) {
        std::cerr << "[" << get_name() << "] CRITICAL: Failed to create MPV Render Context!" << std::endl;
        return false;
    }

    std::cout << "[" << get_name() << "] MPV hardware backend initialized." << std::endl;
    return true;
}

// ------------------------------------------------------------------------------
// FBO MANAGEMENT
// ------------------------------------------------------------------------------
void VideoBgEffect::reallocate_fbo(int w, int h) {
    if (w == video_w && h == video_h && video_fbo != 0) return;
    
    if (video_fbo) glDeleteFramebuffers(1, &video_fbo);
    if (video_tex) glDeleteTextures(1, &video_tex);

    video_w = w;
    video_h = h;

    glGenTextures(1, &video_tex);
    glBindTexture(GL_TEXTURE_2D, video_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, video_w, video_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glGenFramebuffers(1, &video_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, video_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, video_tex, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// ------------------------------------------------------------------------------
// MAIN RENDER LOOP
// ------------------------------------------------------------------------------
void VideoBgEffect::render(uint32_t width, uint32_t height, float dt) {
    if (!mpv_gl) return;

    if (video_path_pending && !video_path.empty()) {
        video_path_pending = false;
        
        std::string fp = video_path;
        if (fp.find("http") != 0 && fp[0] != '/') {
            const char* bundle = m_core->get_bundle_path(get_name());
            if (bundle && std::strlen(bundle) > 0) {
                fp = std::string(bundle) + "/" + fp;
            }
        }

        if (fp.length() > 2) {
            std::cout << "\033[36m[" << get_name() << "] Loading media: '" << fp << "'\033[0m" << std::endl;
            // Единственный вызов загрузки (без сломанного append)
            const char* cmd[] = {"loadfile", fp.c_str(), "replace", nullptr};
            mpv_command_async(mpv, 0, cmd);
        }
    }

    while (mpv_event* event = mpv_wait_event(mpv, 0.0)) {
        if (event->event_id == MPV_EVENT_NONE) break;
        if (event->event_id == MPV_EVENT_LOG_MESSAGE && debug_mpv) {
            auto* msg = static_cast<mpv_event_log_message*>(event->data);
            std::cout << "\033[90m[MPV] " << msg->prefix << ": " << msg->text << "\033[0m"; 
        }
    }

    uint64_t update_flags = mpv_render_context_update(mpv_gl);
    
    // MPV_RENDER_UPDATE_FRAME сигнализирует, что драйвер декодировал новый кадр
    if (update_flags & MPV_RENDER_UPDATE_FRAME) {
        int64_t w = width, h = height;
        mpv_get_property(mpv, "video-params/dw", MPV_FORMAT_INT64, &w);
        mpv_get_property(mpv, "video-params/dh", MPV_FORMAT_INT64, &h);

        if (w > 0 && h > 0) {
            reallocate_fbo(w, h);

            GLint prev_fbo;
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prev_fbo);
            
            glBindFramebuffer(GL_FRAMEBUFFER, video_fbo);

            mpv_opengl_fbo fbo_ctx{
                static_cast<int>(video_fbo),
                static_cast<int>(video_w),
                static_cast<int>(video_h),
                GL_RGBA8 
            };

            // 0 - Нативный FBO маппинг. Шейдер перевернет его как надо.
            int flip_y = 0; 

            mpv_render_param render_params[] = {
                {MPV_RENDER_PARAM_OPENGL_FBO, &fbo_ctx},
                {MPV_RENDER_PARAM_FLIP_Y, &flip_y},
                {MPV_RENDER_PARAM_INVALID, nullptr}
            };

            mpv_render_context_render(mpv_gl, render_params);

            glBindFramebuffer(GL_FRAMEBUFFER, prev_fbo);
        }
    }

    glUseProgram(program);
    time += dt; 
    
    float s_aspect = 1.0f;
    if (height > 0) s_aspect = static_cast<float>(width) / static_cast<float>(height);
    
    float t_aspect = 1.0f;
    if (video_h > 0) t_aspect = static_cast<float>(video_w) / static_cast<float>(video_h);
    
    if (u_time != -1) glUniform1f(u_time, time);
    if (u_resolution != -1) glUniform2f(u_resolution, static_cast<float>(width), static_cast<float>(height));
    
    if (u_pixel_size != -1) glUniform2f(u_pixel_size, 2.0f / width, 2.0f / height);
    if (u_screen_aspect != -1) glUniform1f(u_screen_aspect, s_aspect);
    if (u_tex_aspect != -1) glUniform1f(u_tex_aspect, t_aspect);

    if (u_fill_mode != -1) glUniform1i(u_fill_mode, fill_mode);
    if (u_border_radius != -1) glUniform1f(u_border_radius, border_radius);
    if (u_scale != -1) glUniform1f(u_scale, scale);
    if (u_offset != -1) glUniform2fv(u_offset, 1, &offset[0]);
    if (u_rotation != -1) glUniform1f(u_rotation, rotation);
    
    if (u_brightness != -1) glUniform1f(u_brightness, brightness);
    if (u_contrast != -1) glUniform1f(u_contrast, contrast);
    if (u_saturation != -1) glUniform1f(u_saturation, saturation);
    if (u_tint_color != -1) glUniform3fv(u_tint_color, 1, &tint_color[0]);
    if (u_tint_intensity != -1) glUniform1f(u_tint_intensity, tint_intensity);
    
    if (u_blur_radius != -1) glUniform1f(u_blur_radius, blur_radius);

    if (video_tex) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, video_tex);
        if (u_base_image != -1) glUniform1i(u_base_image, 0);
    }

    glDisable(GL_DEPTH_TEST);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
    
    glActiveTexture(GL_TEXTURE0);
    glEnable(GL_DEPTH_TEST);
}

void VideoBgEffect::on_cleanup() {
    bool is_gl_alive = (eglGetCurrentContext() != EGL_NO_CONTEXT);

    if (mpv_gl) {
        // АРХИТЕКТУРНЫЙ ФИКС: Удаляем ресурсы OpenGL только если контекст физически жив.
        // Иначе (при выходе из сна) драйвер Mesa/NVIDIA упадет в жесткий Segfault.
        if (is_gl_alive) {
            mpv_render_context_free(mpv_gl);
        } else {
            std::cerr << "\033[33m[" << get_name() << "] Warning: Leaking mpv_gl context to prevent driver crash during EGL recovery.\033[0m" << std::endl;
        }
        mpv_gl = nullptr;
    }

    if (mpv) {
        // Теперь этот вызов 100% безопасен и не блокирует Wayland сеанс.
        mpv_terminate_destroy(mpv);
        mpv = nullptr;
    }

    if (is_gl_alive) {
        if (video_fbo) { glDeleteFramebuffers(1, &video_fbo); video_fbo = 0; }
        if (video_tex) { glDeleteTextures(1, &video_tex); video_tex = 0; }
        
        if (program) { glDeleteProgram(program); program = 0; }
        if (vao) { glDeleteVertexArrays(1, &vao); vao = 0; }
    }
}

extern "C" {
    uint32_t get_abi_version() { return SHADER_DESK_ABI_VERSION; }
    IWallpaperEffectABI* create_effect() { return new VideoBgEffect(); }
    void destroy_effect(IWallpaperEffectABI* effect) { delete static_cast<VideoBgEffect*>(effect); }
}