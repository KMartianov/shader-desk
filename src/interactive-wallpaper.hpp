// src/interactive-wallpaper.hpp

#pragma once

#include <iostream> 
#include <wayland-client.h>
#include <wayland-egl.h>
#include <EGL/egl.h>
#include <string>
#include <memory>
#include <unordered_map>
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "viewporter-client-protocol.h"
#include "pointer-daemon-client.hpp"
#include <thread>          
#include <atomic>          
#include <chrono>          
#include <mutex>           
#include <nlohmann/json.hpp>
#include "wallpaper-effect.hpp"
#include "audio-daemon-client.hpp"
#include "audio-data.hpp"

enum class RendererType {
    OPENGL_ES,
    VULKAN,
    SOFTWARE
};

struct WallpaperConfig {
    std::string output_name = "*";
    RendererType renderer = RendererType::OPENGL_ES;
    bool interactive = true;
};

EffectParameterValue json_to_variant(const nlohmann::json& j);


class InteractiveWallpaper {
public:
    struct Output {
        InteractiveWallpaper* parent = nullptr;
        std::string name;
        std::string identifier;
        wl_output* output_obj = nullptr;
        wl_surface* surface = nullptr;
        zwlr_layer_surface_v1* layer_surface = nullptr;

        uint32_t width = 0;
        uint32_t height = 0;
        int32_t scale = 1;
        uint32_t configure_serial = 0;
        bool configured = false;
        
        WallpaperEffectPtr effect;

        // EGL resources
        wl_egl_window* egl_window = nullptr;
        EGLSurface egl_surface = EGL_NO_SURFACE;
        
        Output() : effect(nullptr, nullptr) {}
    };

    InteractiveWallpaper(const WallpaperConfig& cfg);
    ~InteractiveWallpaper();

    bool initialize();
    void run();
    void stop();

    void set_effect(const std::string& output_name, WallpaperEffectPtr effect);

    wl_shm* get_shm() const { return shm; }

    // Wayland listeners (перемещены в public)
    static void registry_global(void* data, wl_registry* registry,
                                uint32_t name, const char* interface, uint32_t version);
    static void registry_global_remove(void* data, wl_registry* registry, uint32_t name);

    // Output listeners
    static void output_geometry(void* data, wl_output* wl_output,
                                int32_t x, int32_t y, int32_t width_mm, int32_t height_mm,
                                int32_t subpixel, const char* make, const char* model,
                                int32_t transform);
    static void output_mode(void* data, wl_output* wl_output, uint32_t flags,
                            int32_t width, int32_t height, int32_t refresh);
    static void output_done(void* data, wl_output* wl_output);
    static void output_scale(void* data, wl_output* wl_output, int32_t scale);
    static void output_name(void* data, wl_output* wl_output, const char* name);
    static void output_description(void* data, wl_output* wl_output, const char* description);
    void process_pointer_motion(double dx, double dy, bool is_touchpad);


    // Layer surface listeners
    static void layer_surface_configure(void* data, zwlr_layer_surface_v1* surface,
                                        uint32_t serial, uint32_t width, uint32_t height);
    static void layer_surface_closed(void* data, zwlr_layer_surface_v1* surface);

    bool reload_config();
    void start_config_monitor();
    void stop_config_monitor();
    


private:
    // Wayland globals
    wl_display* display = nullptr;
    wl_compositor* compositor = nullptr;
    wl_shm* shm = nullptr;
    zwlr_layer_shell_v1* layer_shell = nullptr;
    wp_viewporter* viewporter = nullptr;

    std::atomic<bool> config_needs_apply{false}; 
    // Input handling
    //InputHandler input_handler;

    // EGL context
    EGLDisplay egl_display = EGL_NO_DISPLAY;
    EGLContext egl_context = EGL_NO_CONTEXT;
    EGLConfig egl_config = nullptr;

    WallpaperConfig config;
    std::unordered_map<wl_output*, std::unique_ptr<Output>> outputs;
    bool running = true;

    PointerDaemonClient pointer_daemon;
    bool use_pointer_daemon = true;

    float touchpad_sensitivity = 0.3f;
    float mouse_sensitivity = 2.5f;
    float sphere_scale = 1.0f;

    // Internal methods
    bool init_egl();
    void create_egl_surface(Output* output);
    void create_layer_surface(Output* output);
    
    void check_egl_error(const std::string& operation);
    void apply_config_to_effect(Output* output);
    void init_pointer_daemon();
    void handle_daemon_motion(double dx, double dy, double vx, double vy, double dt, bool normalized, const std::string& device_name);

    std::unique_ptr<AudioDaemonClient> audio_client_; // <-- Добавляем клиент
    void handle_audio_data(const AudioData& data);    // <-- Метод-обработчик данных



    std::thread config_monitor_thread;
    std::atomic<bool> config_monitor_running{false};
    std::chrono::system_clock::time_point last_config_modification;
    std::mutex config_mutex;
    nlohmann::json current_config;

};

