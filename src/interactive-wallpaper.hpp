// src/interactive-wallpaper.hpp
#pragma once

// Системные библиотеки
#include <iostream> 
#include <string>
#include <memory>
#include <unordered_map>
#include <functional>

// Заголовки для работы с epoll и inotify (Zero-latency I/O)
#include <sys/epoll.h>
#include <sys/inotify.h>

// Wayland и EGL
#include <wayland-client.h>
#include <wayland-egl.h>
#include <EGL/egl.h>
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "viewporter-client-protocol.h"

#include "lua-engine.hpp"

// Внутренние компоненты (обрати внимание, демонов ввода больше нет!)
#include "core-context.hpp"    // Интерфейс Ядра и BlackBoard
#include "wallpaper-effect.hpp" // Интерфейс визуальных плагинов
#include "plugin-manager.hpp"   // Загрузчик .so библиотек

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


// ==============================================================================
// ГЛАВНЫЙ КЛАСС ЯДРА
// Теперь он наследует ICoreContext, предоставляя плагинам доступ к памяти и epoll
// ==============================================================================
class InteractiveWallpaper : public ICoreContextABI {
public:
    // Структура, описывающая один физический монитор (wl_output)
    struct Output {
        InteractiveWallpaper* parent = nullptr;
        std::string name;
        std::string identifier;
        
        // Wayland объекты
        wl_output* output_obj = nullptr;
        wl_surface* surface = nullptr;
        zwlr_layer_surface_v1* layer_surface = nullptr;

        uint32_t width = 0;
        uint32_t height = 0;
        int32_t scale = 1;
        uint32_t configure_serial = 0;
        bool configured = false;
        
        // Экземпляр визуального эффекта (плагина), привязанный к этому монитору
        WallpaperEffectPtr effect;

        // EGL ресурсы для рендеринга на этот монитор
        wl_egl_window* egl_window = nullptr;
        EGLSurface egl_surface = EGL_NO_SURFACE;

        wl_callback* frame_callback = nullptr;
        
        Output() : effect(nullptr, nullptr) {}

        ~Output() {
            if (effect) { effect->cleanup(); effect.reset(); }
            if (egl_surface != EGL_NO_SURFACE && parent) eglDestroySurface(parent->egl_display, egl_surface);
            if (egl_window) wl_egl_window_destroy(egl_window);
            if (frame_callback) wl_callback_destroy(frame_callback);
            if (layer_surface) zwlr_layer_surface_v1_destroy(layer_surface);
            if (surface) wl_surface_destroy(surface);
            if (output_obj) wl_output_release(output_obj);
        }
    };
    
    InteractiveWallpaper(const WallpaperConfig& cfg, LuaEngine& engine);

    ~InteractiveWallpaper() override; // Добавлен override деструктора

    bool initialize();
    void run();   // Главный цикл программы (zero-latency epoll loop)
    void stop();

    static void frame_handle_done(void* data, wl_callback* callback, uint32_t time);
    void render_output(Output* output);

    void set_plugin_manager(PluginManager* pm, const std::string& effect_name);
    void apply_effect_to_output(Output* output);

    wl_shm* get_shm() const { return shm; }

    // --- Реализация интерфейса ICoreContextABI (Для плагинов) ---
    IBlackBoardABI* get_blackboard() override;
    
    void register_epoll_fd(int fd, void (*callback)(uint32_t events, void* user_data), void* user_data) override;
    void unregister_epoll_fd(int fd) override;

    // --- Внутренний метод Ядра (Для LuaEngine, Inotify и т.д.) ---
    void register_epoll_fd_cxx(int fd, std::function<void(uint32_t)> callback);

    // --- Wayland Listeners (Статические коллбэки) ---
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
    // --- Архитектура Data-Driven (BlackBoard) ---
    BlackBoard blackboard; 
    std::unordered_map<int, std::function<void(uint32_t)>> epoll_callbacks;

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

    // --- Состояние приложения ---
    WallpaperConfig config;
    std::unordered_map<wl_output*, std::unique_ptr<Output>> outputs;
    bool running = true;

    // --- Файловые дескрипторы для мультиплексирования (epoll) ---
    int epoll_fd = -1;
    int inotify_fd = -1;

    PluginManager* plugin_manager_ = nullptr;
    std::string current_effect_name_;

    LuaEngine& lua_engine;


    // --- Внутренние методы ---
    bool init_egl();
    void create_egl_surface(Output* output);
    void create_layer_surface(Output* output);
    void check_egl_error(const std::string& operation);
    
    // --- Инициализация и обработка событий Inotify ---
    void setup_inotify();
    void process_inotify_events();
    
    // --- Применение конфигурации ---
    void apply_config_to_all_outputs();
    void apply_config_to_effect(Output* output);
};