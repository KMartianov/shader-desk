// Src/interactive-wallpaper.hpp
#pragma once

// System libraries
#include <iostream> 
#include <string>
#include <memory>
#include <unordered_map>
#include <functional>

// Headers for epoll and inotify (Zero-latency I/O multiplexing)
#include <sys/epoll.h>
#include <sys/inotify.h>

// Wayland and EGL
#include <wayland-client.h>
#include <wayland-egl.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h> 
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "viewporter-client-protocol.h"
#include <sys/stat.h>

#include "lua-engine.hpp"

#include <chrono>

// Internal components (legacy input daemons are removed)
#include "core-context.hpp"    // Core Interface & BlackBoard
#include "wallpaper-effect.hpp" // Visual plugin interfaces
#include "plugin-manager.hpp"   // Dynamic .so loader

enum class RendererType {
    OPENGL_ES,
    VULKAN,
    SOFTWARE
};

struct LayerInstance {
    std::string name;
    std::string tag;
    WallpaperEffectPtr effect;
    bool is_postprocess;
    bool clear_depth; // Z-Buffer management flag
    
    //  Constructor
    LayerInstance(std::string n, std::string t, WallpaperEffectPtr eff, bool post, bool c_depth = true) 
        : name(std::move(n)), tag(std::move(t)), effect(std::move(eff)), is_postprocess(post), clear_depth(c_depth) {}
};



struct WallpaperConfig {
    std::string output_name = "*";
    RendererType renderer = RendererType::OPENGL_ES;
    bool interactive = true;
};


// ==============================================================================
// MAIN CORE CLASS
// Inherits ICoreContextABI to provide plugins with memory and epoll access.
// ==============================================================================
class InteractiveWallpaper : public ICoreContextABI {
public:
    // Structure describing a single physical monitor (wl_output)
    struct Output {
        InteractiveWallpaper* parent = nullptr;
        std::string name;
        std::string identifier;

        std::chrono::steady_clock::time_point last_frame_time = std::chrono::steady_clock::now();
        
        // Wayland objects
        wl_output* output_obj = nullptr;
        wl_surface* surface = nullptr;
        zwlr_layer_surface_v1* layer_surface = nullptr;

        uint32_t width = 0;
        uint32_t height = 0;
        int32_t scale = 1;
        uint32_t configure_serial = 0;
        bool configured = false;

        float current_fps_limit = 0.0f;
        
        // Visual effect plugin instance bound to this specific monitor
        std::vector<LayerInstance> layers;

        GLuint fbo[2] = {0, 0};
        GLuint tex[2] = {0, 0};
        GLuint depth_rbo[2] = {0, 0};
        
        
        GLuint tex_feedback = 0;

        int current_fbo = 0;
        uint32_t fbo_w = 0, fbo_h = 0; // Internal FBO resolution

        void allocate_fbos(uint32_t w, uint32_t h);
        void destroy_fbos();
        
        


        // EGL resources for rendering on this monitor
        wl_egl_window* egl_window = nullptr;
        EGLSurface egl_surface = EGL_NO_SURFACE;

        wl_callback* frame_callback = nullptr;
        int fps_timer_fd = -1;
        
        Output() {
            fps_timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
        }

        ~Output() {
            layers.clear(); // Safe plugin deletion
            // Destroy EGL and FBO
            if (parent && egl_surface != EGL_NO_SURFACE) {
                eglMakeCurrent(parent->egl_display, egl_surface, egl_surface, parent->egl_context);
                destroy_fbos();
                eglDestroySurface(parent->egl_display, egl_surface);
            }
            if (egl_window) wl_egl_window_destroy(egl_window);
            if (frame_callback) wl_callback_destroy(frame_callback);
            if (layer_surface) zwlr_layer_surface_v1_destroy(layer_surface);
            if (surface) wl_surface_destroy(surface);
            if (output_obj) wl_output_release(output_obj);
            if (fps_timer_fd >= 0) {if (parent) parent->unregister_epoll_fd(fps_timer_fd); close(fps_timer_fd); }
        }
    };
    
    InteractiveWallpaper(const WallpaperConfig& cfg, LuaEngine& engine);

    ~InteractiveWallpaper() override; // Added destructor override

    bool initialize();
    void run();   // Main event loop (zero-latency epoll based)
    void stop();

    const char* get_bundle_path(const char* plugin_name) override;

    static void frame_handle_done(void* data, wl_callback* callback, uint32_t time);
    void render_output(Output* output);

    void set_plugin_manager(PluginManager* pm, const std::string& effect_name);
    void apply_effect_to_output(Output* output);

    wl_shm* get_shm() const { return shm; }

    // --- ICoreContextABI Implementation (For plugins) ---
    IBlackBoardABI* get_blackboard() override;
    
    void register_epoll_fd(int fd, void (*callback)(uint32_t events, void* user_data), void* user_data) override;
    void unregister_epoll_fd(int fd) override;

    // --- Internal Core Method (For LuaEngine, Inotify, etc.) ---
    void register_epoll_fd_cxx(int fd, std::function<void(uint32_t)> callback);

    void log_message(LogLevel level, const char* source, const char* message) override;
    void* get_native_display() override { return display; }

    // --- Wayland Listeners (Static callbacks) ---
    static void registry_global(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version);
    static void registry_global_remove(void* data, wl_registry* registry, uint32_t name);

    static void output_geometry(void* data, wl_output* wl_output, int32_t x, int32_t y, int32_t width_mm, int32_t height_mm, int32_t subpixel, const char* make, const char* model, int32_t transform);
    static void output_mode(void* data, wl_output* wl_output, uint32_t flags, int32_t width, int32_t height, int32_t refresh);
    static void output_done(void* data, wl_output* wl_output);
    static void output_scale(void* data, wl_output* wl_output, int32_t scale);
    static void output_name(void* data, wl_output* wl_output, const char* name);
    static void output_description(void* data, wl_output* wl_output, const char* description);

    static void layer_surface_configure(void* data, zwlr_layer_surface_v1* surface, uint32_t serial, uint32_t width, uint32_t height);
    static void layer_surface_closed(void* data, zwlr_layer_surface_v1* surface);


private:
    // --- Data-Driven Architecture (BlackBoard) ---
    BlackBoard blackboard; 
    std::unordered_map<int, std::function<void(uint32_t)>> epoll_callbacks;
    std::unordered_map<int, std::string> ipc_buffers;

    // --- Wayland Globals ---
    wl_display* display = nullptr;
    wl_compositor* compositor = nullptr;
    wl_shm* shm = nullptr;
    zwlr_layer_shell_v1* layer_shell = nullptr;
    wp_viewporter* viewporter = nullptr;

    // --- EGL Context ---
    EGLDisplay egl_display = EGL_NO_DISPLAY;
    EGLContext egl_context = EGL_NO_CONTEXT;
    EGLConfig egl_config = nullptr;

    // --- Application state ---
    WallpaperConfig config;
    std::unordered_map<wl_output*, std::unique_ptr<Output>> outputs;
    bool running = true;

    // --- File descriptors for multiplexing (epoll/inotify) ---
    int epoll_fd = -1;
    int inotify_fd = -1;

    int ipc_fd = -1;


    PluginManager* plugin_manager_ = nullptr;
    std::string default_effect_name_;

    LuaEngine& lua_engine;


    // --- Internal Methods ---
    bool init_egl();
    void create_egl_surface(Output* output);
    void create_layer_surface(Output* output);
    void check_egl_error(const std::string& operation);

    void recover_egl_context();

    
    // --- Inotify Initialization & Event Handling ---
    void setup_inotify();
    void setup_ipc();
    void process_inotify_events();
    
    // --- Configuration Application ---
    void apply_config_to_all_outputs();
    void apply_config_to_effect(Output* output);
};