// interactive-wallpaper.cpp
#include "interactive-wallpaper.hpp"
//#include "../plugins/ico-sphere-effect/ico-sphere-effect.hpp"
#include <glm/glm.hpp>

#include <iostream>
#include <cstring>
#include <algorithm>
#include <sys/select.h>
#include <unistd.h>

#include <thread>
#include <chrono>
#include <fstream>
#include <sys/stat.h>
#include <mutex>
#include <atomic>
#include <cstdlib>

#include <nlohmann/json.hpp>
#include "utils.hpp"

using WallpaperEffectPtr = std::unique_ptr<WallpaperEffect, void(*)(WallpaperEffect*)>;

EffectParameterValue json_to_variant(const nlohmann::json& j) {
    if (j.is_boolean()) {
        return j.get<bool>();
    }
    if (j.is_number_integer()) {
        return j.get<int>();
    }
    if (j.is_number_float()) {
        return j.get<float>();
    }
    if (j.is_array() && j.size() == 3) {
        // Убедимся, что все элементы - числа
        if (j[0].is_number() && j[1].is_number() && j[2].is_number()) {
            return glm::vec3(j[0].get<float>(), j[1].get<float>(), j[2].get<float>());
        }
    }
    // Возвращаем float по умолчанию в случае неопределенного или неподдерживаемого типа
    return 0.0f;
}



static const wl_registry_listener registry_listener = {
    .global = InteractiveWallpaper::registry_global,
    .global_remove = InteractiveWallpaper::registry_global_remove,
};

static const wl_output_listener output_listener = {
    .geometry = InteractiveWallpaper::output_geometry,
    .mode = InteractiveWallpaper::output_mode,
    .done = InteractiveWallpaper::output_done,
    .scale = InteractiveWallpaper::output_scale,
    .name = InteractiveWallpaper::output_name,
    .description = InteractiveWallpaper::output_description,
};

static const zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = InteractiveWallpaper::layer_surface_configure,
    .closed = InteractiveWallpaper::layer_surface_closed,
};

// Helper: get user config path
static std::string get_default_config_path() {
    const char* home = std::getenv("HOME");
    if (!home) {
        // fallback to current directory
        return std::string("./interactive-wallpaper-config.json");
    }
    return std::string(home) + "/.config/interactive-wallpaper/config.json";
}

// Constructor
InteractiveWallpaper::InteractiveWallpaper(const WallpaperConfig& cfg) : config(cfg) {
    display = wl_display_connect(nullptr);
    if (!display) {
        std::cerr << "Failed to connect to Wayland display" << std::endl;
        return;
    }
    
   

    if (!init_egl()) {
        std::cerr << "Failed to initialize EGL" << std::endl;
    }
    if (config.interactive && use_pointer_daemon) {
        init_pointer_daemon();
    }

    audio_client_ = std::make_unique<AudioDaemonClient>();
    audio_client_->set_callback([this](const AudioData& data) {
        this->handle_audio_data(data);
    });
    // get_default_audio_socket_path() взята из utils.hpp демона
    if (!audio_client_->connect_and_listen(get_default_audio_socket_path())) {
        std::cerr << "Warning: Could not connect to audio daemon." << std::endl;
    }


    mouse_sensitivity = 0.05f;
    touchpad_sensitivity = 20.0f;
    
    // Запускаем мониторинг конфигурации (горячая перезагрузка)
    start_config_monitor();
}

void InteractiveWallpaper::process_pointer_motion(double dx, double dy, bool is_touchpad) {
    float sensitivity = is_touchpad ? touchpad_sensitivity : mouse_sensitivity;
      
    // Применяем чувствительность
    double effective_dx = dx * sensitivity;
    double effective_dy = dy * sensitivity;
    
    // Передаем обработанное движение эффектам
    for (auto& pair : outputs) {
        auto& output = pair.second;
        if (output->effect) {
            //output->effect->handle_pointer_motion(effective_dx, effective_dy, is_touchpad);
        }
    }
}

// Destructor
InteractiveWallpaper::~InteractiveWallpaper() {
    // Останавливаем мониторинг конфигурации
    
    stop_config_monitor();

    if (audio_client_) {
        audio_client_->disconnect();
    }

    // Clean up EGL resources first
    if (egl_context != EGL_NO_CONTEXT) {
        eglDestroyContext(egl_display, egl_context);
        egl_context = EGL_NO_CONTEXT;
    }
    
    if (egl_display != EGL_NO_DISPLAY) {
        eglTerminate(egl_display);
        egl_display = EGL_NO_DISPLAY;
    }

    // Destroy outputs and associated resources
    for (auto& pair : outputs) {
        auto& output = pair.second;

        // Clean up EGL resources for this output
        if (output->egl_surface != EGL_NO_SURFACE) {
            eglDestroySurface(egl_display, output->egl_surface);
            output->egl_surface = EGL_NO_SURFACE;
        }
        
        if (output->egl_window) {
            wl_egl_window_destroy(output->egl_window);
            output->egl_window = nullptr;
        }

        // Clean up effect
        if (output->effect) {
            output->effect->cleanup();
            output->effect.reset();
        }

        // Clean up Wayland resources
        if (output->layer_surface) {
            zwlr_layer_surface_v1_destroy(output->layer_surface);
            output->layer_surface = nullptr;
        }

        if (output->surface) {
            wl_surface_destroy(output->surface);
            output->surface = nullptr;
        }

        if (output->output_obj) {
            wl_output_destroy(output->output_obj);
            output->output_obj = nullptr;
        }
    }
    outputs.clear();

    if (layer_shell) {
        zwlr_layer_shell_v1_destroy(layer_shell);
        layer_shell = nullptr;
    }
    if (compositor) {
        wl_compositor_destroy(compositor);
        compositor = nullptr;
    }
    if (shm) {
        wl_shm_destroy(shm);
        shm = nullptr;
    }
    if (viewporter) {
        wp_viewporter_destroy(viewporter);
        viewporter = nullptr;
    }
    if (display) {
        wl_display_disconnect(display);
        display = nullptr;
    }
    pointer_daemon.disconnect();

}

void InteractiveWallpaper::handle_audio_data(const AudioData& data) {
    // Передаем данные каждому активному эффекту на каждом мониторе
    for (auto& pair : outputs) {
        auto& output = pair.second;
        if (output && output->effect) {
            output->effect->handle_audio_data(data);
        }
    }
}


void InteractiveWallpaper::init_pointer_daemon() {
    pointer_daemon.set_callbacks(
        [this](double dx, double dy, double vx, double vy, double dt, bool normalized, const std::string& device_name) {
            this->handle_daemon_motion(dx, dy, vx, vy, dt, normalized, device_name);
            
        },
        // 2. MoveCallback (добавили nullptr)
        nullptr,
        // 3. ClickCallback (добавили nullptr)
        nullptr
    );
    
    if (!pointer_daemon.connect()) {
        std::cerr << "Failed to connect to pointer daemon, continuing without mouse input" << std::endl;
        use_pointer_daemon = false;
    } else {
        std::cout << "Pointer daemon connected successfully" << std::endl;
    }
}

void InteractiveWallpaper::handle_daemon_motion(double dx, double dy, double vx, double vy, double dt, 
                                               bool normalized, const std::string& device_name) {
    // Логируем информацию об устройстве
    //std::cout << "Daemon motion - Device: " << device_name 
    //          << " Norm: " << (normalized ? "yes" : "no")
    //          << " dX: " << dx << " dY: " << dy 
    //          << " vx: " << vx << " vy: " << vy << " dt: " << dt << std::endl;
    
    // Определяем тип устройства на основе normalized флага
    bool is_touchpad = normalized;
    float sensitivity = is_touchpad ? touchpad_sensitivity : mouse_sensitivity;
    

    
    double effective_dx, effective_dy;
    
    // Выбираем стратегию обработки в зависимости от наличия данных о скорости
    if (std::abs(vx) > 1e-6 && std::abs(dt) > 1e-6) {
        // Используем скорость для плавного вращения
        effective_dx = vx * sensitivity * dt;
        effective_dy = vy * sensitivity * dt;
    } else {
        // Используем сырое смещение
        effective_dx = dx * sensitivity;
        effective_dy = dy * sensitivity;
    }
              
    // Передаем обработанное движение эффектам
    for (auto& pair : outputs) {
        auto& output = pair.second;
        if (output->effect) {
            output->effect->handle_pointer_motion(effective_dx, effective_dy, is_touchpad);
        }
    }
}


// ------------------------- Конфигурация и мониторинг -------------------------

bool InteractiveWallpaper::reload_config() {
    const std::string config_path = get_default_config_path();

    try {
        std::ifstream config_file(config_path);
        if (!config_file.is_open()) {
            std::cerr << "Cannot open config file: " << config_path << std::endl;
            return false;
        }
        
        nlohmann::json new_config = nlohmann::json::parse(config_file);

        {
            std::lock_guard<std::mutex> lock(config_mutex);
            current_config = std::move(new_config);
        }

        std::cout << "Configuration reloaded successfully: " << config_path << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error reloading config: " << e.what() << std::endl;
        return false;
    }
}

void InteractiveWallpaper::check_egl_error(const std::string& operation) {
    EGLint error = eglGetError();
    if (error != EGL_SUCCESS) {
        std::cerr << "EGL error during " << operation << ": " << error << std::endl;
    }
}


void InteractiveWallpaper::apply_config_to_effect(Output* output) {
    if (!output || !output->effect) {
        return;
    }

    // Блокируем мьютекс для безопасного доступа к конфигурации из другого потока
    std::lock_guard<std::mutex> lock(config_mutex);
    if (current_config.is_null()) {
        std::cout << "apply_config_to_effect: current_config is null, nothing to apply" << std::endl;
        return;
    }

    std::cout << "Applying configuration to effect on output: " << output->name << std::endl;

    // --- Применение настроек самого эффекта ---
    if (current_config.contains("effect_settings") && current_config["effect_settings"].is_object()) {
        const auto& settings = current_config["effect_settings"];
        for (const auto& item : settings.items()) {
            std::cout << "  - Setting '" << item.key() << "'..." << std::endl;
            // Используем универсальный метод set_parameter интерфейса WallpaperEffect
            output->effect->set_parameter(item.key(), json_to_variant(item.value()));
        }
    }

    // --- Применение глобальных настроек приложения ---
    if (current_config.contains("touchpad_sensitivity")) {
        touchpad_sensitivity = current_config.value("touchpad_sensitivity", 20.0f);
        std::cout << "  - Global: Touchpad sensitivity set to " << touchpad_sensitivity << std::endl;
    }

    if (current_config.contains("mouse_sensitivity")) {
        mouse_sensitivity = current_config.value("mouse_sensitivity", 0.05f);
        std::cout << "  - Global: Mouse sensitivity set to " << mouse_sensitivity << std::endl;
    }

    std::cout << "Configuration applied successfully on output: " << output->name << std::endl;
}



void InteractiveWallpaper::start_config_monitor() {
    bool expected = false;
    if (!config_monitor_running.compare_exchange_strong(expected, true)) {
        return;
    }

    config_monitor_thread = std::thread([this]() {
        const std::string config_path = get_default_config_path();

        struct stat st;
        if (stat(config_path.c_str(), &st) == 0) {
            last_config_modification = std::chrono::system_clock::from_time_t(st.st_mtime);
        } else {
            last_config_modification = std::chrono::system_clock::from_time_t(0);
        }

        // Первичная загрузка
        if (reload_config()) {
            config_needs_apply.store(true);
            std::cout << "Initial configuration loaded, will apply in main thread" << std::endl;
        }

        while (config_monitor_running.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000)); // 1 секунда

            struct stat current_st;
            if (stat(config_path.c_str(), &current_st) != 0) {
                continue;
            }

            auto current_modification = std::chrono::system_clock::from_time_t(current_st.st_mtime);
            if (current_modification > last_config_modification) {
                last_config_modification = current_modification;
                std::cout << "Config file modified, reloading: " << config_path << std::endl;
                if (reload_config()) {
                    config_needs_apply.store(true);
                } else {
                    std::cerr << "Failed to reload config after modification." << std::endl;
                }
            }
        }
    });
}


void InteractiveWallpaper::stop_config_monitor() {
    bool expected = true;
    if (!config_monitor_running.compare_exchange_strong(expected, false)) {
        // монитор уже остановлен
        return;
    }

    if (config_monitor_thread.joinable()) {
        config_monitor_thread.join();
    }
}

// ------------------------- EGL initialization -------------------------

bool InteractiveWallpaper::init_egl() {
    egl_display = eglGetDisplay((EGLNativeDisplayType)display);
    if (egl_display == EGL_NO_DISPLAY) {
        std::cerr << "Failed to get EGL display" << std::endl;
        return false;
    }

    EGLint major, minor;
    if (!eglInitialize(egl_display, &major, &minor)) {
        std::cerr << "Failed to initialize EGL" << std::endl;
        return false;
    }
    
    std::cout << "EGL initialized: version " << major << "." << minor << std::endl;

    // Use EGL_OPENGL_ES3_BIT for GLES 3.0
    const EGLint config_attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 24,
        EGL_STENCIL_SIZE, 0,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_NONE
    };

    EGLint num_configs;
    if (!eglChooseConfig(egl_display, config_attribs, &egl_config, 1, &num_configs)) {
        std::cerr << "Failed to choose EGL config" << std::endl;
        return false;
    }

    if (num_configs == 0) {
        std::cerr << "No matching EGL config found" << std::endl;
        return false;
    }

    // Create OpenGL ES 3.0 context
    const EGLint context_attribs[] = {
        EGL_CONTEXT_MAJOR_VERSION, 3,
        EGL_CONTEXT_MINOR_VERSION, 0,
        EGL_NONE
    };

    egl_context = eglCreateContext(egl_display, egl_config, EGL_NO_CONTEXT, context_attribs);
    if (egl_context == EGL_NO_CONTEXT) {
        std::cerr << "Failed to create EGL context (OpenGL ES 3.0)" << std::endl;
        
        // Fallback to OpenGL ES 2.0
        const EGLint context_attribs_es2[] = {
            EGL_CONTEXT_MAJOR_VERSION, 2,
            EGL_NONE
        };
        
        egl_context = eglCreateContext(egl_display, egl_config, EGL_NO_CONTEXT, context_attribs_es2);
        if (egl_context == EGL_NO_CONTEXT) {
            std::cerr << "Failed to create EGL context (OpenGL ES 2.0 fallback)" << std::endl;
            return false;
        }
        std::cout << "Using OpenGL ES 2.0 context" << std::endl;
    } else {
        std::cout << "Using OpenGL ES 3.0 context" << std::endl;
    }

    return true;
}

// Create EGL surface for output
void InteractiveWallpaper::create_egl_surface(Output* output) {
    if (!output || !output->surface || output->width == 0 || output->height == 0) {
        std::cerr << "Invalid parameters for EGL surface creation" << std::endl;
        return;
    }

    // Create new EGL window
    output->egl_window = wl_egl_window_create(output->surface, output->width, output->height);
    if (!output->egl_window) {
        std::cerr << "Failed to create EGL window\n";
        return;
    }

    // Create EGL surface
    output->egl_surface = eglCreateWindowSurface(egl_display, egl_config, 
                                                (EGLNativeWindowType)output->egl_window, nullptr);
    if (output->egl_surface == EGL_NO_SURFACE) {
        std::cerr << "Failed to create EGL surface: error " << eglGetError() << std::endl;
        wl_egl_window_destroy(output->egl_window);
        output->egl_window = nullptr;
        return;
    }

    std::cout << "Created EGL surface for output: " << output->name 
              << " (" << output->width << "x" << output->height << ")" << std::endl;
}


// Initialize Wayland connection
bool InteractiveWallpaper::initialize() {
    if (!display) {
        std::cerr << "No Wayland display connection" << std::endl;
        return false;
    }

    struct wl_registry* registry = wl_display_get_registry(display);
    if (!registry) {
        std::cerr << "Failed to get Wayland registry" << std::endl;
        return false;
    }

    wl_registry_add_listener(registry, &registry_listener, this);
    wl_display_roundtrip(display);
    wl_registry_destroy(registry);

    if (!compositor || !layer_shell) {
        std::cerr << "Missing required Wayland interfaces (compositor or layer_shell)" << std::endl;
        return false;
    }

    std::cout << "InteractiveWallpaper initialized successfully" << std::endl;
    return true;
}

void InteractiveWallpaper::run() {
    if (!display) return;

    std::cout << "Starting main loop..." << std::endl;
    wl_display_roundtrip(display);

    bool first_render = true;

    while (running) {
        // Нам нужно сбросить флаг только после того, как все мониторы применили конфиг.
        // Поэтому мы используем локальную переменную.
        bool config_applied_in_this_cycle = false;
        
        // Проверяем, есть ли запрос на применение конфигурации
        bool needs_apply = config_needs_apply.load() || first_render;

        while (wl_display_prepare_read(display) != 0) {
            wl_display_dispatch_pending(display);
        }
        wl_display_flush(display);
        
        struct timeval tv = {0, 16000};
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(wl_display_get_fd(display), &fds);
        
        if (select(wl_display_get_fd(display) + 1, &fds, NULL, NULL, &tv) > 0) {
            wl_display_read_events(display);
            wl_display_dispatch_pending(display);
        } else {
            wl_display_cancel_read(display);
        }

        // Рендеринг
        bool has_outputs = false;
        for (auto& pair : outputs) {
            auto& output = pair.second;
            if (output->configured && output->effect && output->egl_surface != EGL_NO_SURFACE) {
                has_outputs = true;
                
                if (eglMakeCurrent(egl_display, output->egl_surface, output->egl_surface, egl_context)) {
                    
                    // Применяем конфигурацию в единственно безопасном месте:
                    // после eglMakeCurrent и прямо перед рендерингом.
                    if (needs_apply) {
                        apply_config_to_effect(output.get());
                        config_applied_in_this_cycle = true;
                    }

                    output->effect->render(output->width, output->height);
                    eglSwapBuffers(egl_display, output->egl_surface);
                }
            }
        }

        // Если мы в этом цикле применили конфиг, то теперь можно сбросить глобальный флаг
        if (config_applied_in_this_cycle) {
            config_needs_apply.store(false);
            first_render = false;
        }

        if (!has_outputs) {
            usleep(100000);
        }
    }

    std::cout << "Main loop exited" << std::endl;
}

void InteractiveWallpaper::stop() {
    running = false;
}

// Effect management
void InteractiveWallpaper::set_effect(const std::string& output_name, WallpaperEffectPtr effect) {
    for (auto& pair : outputs) {
        auto& output = pair.second;
        if (output_name == "*" || output->name == output_name || output->identifier == output_name) {
            // Clean up existing effect
            if (output->effect) {
                // The custom deleter in the old unique_ptr will be called automatically
                // when it's replaced by the new one below.
            }
            
            output->effect = std::move(effect);
            
            if (output->configured && output->effect && output->egl_surface != EGL_NO_SURFACE) {
                // Make context current before initializing effect
                if (eglMakeCurrent(egl_display, output->egl_surface, output->egl_surface, egl_context)) {
                    output->effect->initialize(output->width, output->height);
                } else {
                    std::cerr << "Failed to make context current for effect initialization" << std::endl;
                }
            }
            
            std::cout << "Set effect for output: " << output->name << std::endl;
            // Since we moved the effect, we might only want to set it on the first match.
            // If you want the same instance on all monitors, you can't move it.
            // For now, let's assume we create a new effect instance for each monitor,
            // so moving it into the first one is correct. If you need it on all monitors,
            // the logic in main.cpp should change to create an effect for each one.
            return; // <-- Added to prevent moving the same pointer multiple times
        }
    }
}



// Wayland registry handler
void InteractiveWallpaper::registry_global(void* data, wl_registry* registry,
                                          uint32_t name, const char* interface, uint32_t version) {
    InteractiveWallpaper* self = static_cast<InteractiveWallpaper*>(data);

    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        self->compositor = static_cast<wl_compositor*>(
            wl_registry_bind(registry, name, &wl_compositor_interface, std::min(version, 4u)));
        std::cout << "Bound wl_compositor" << std::endl;
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        self->shm = static_cast<wl_shm*>(
            wl_registry_bind(registry, name, &wl_shm_interface, std::min(version, 1u)));
        std::cout << "Bound wl_shm" << std::endl;
    } else if (strcmp(interface, wl_output_interface.name) == 0) {
        auto output = std::make_unique<Output>();
        output->parent = self;
        output->output_obj = static_cast<wl_output*>(
            wl_registry_bind(registry, name, &wl_output_interface, std::min(version, 4u)));
        wl_output_add_listener(output->output_obj, &output_listener, output.get());
        self->outputs[output->output_obj] = std::move(output);
        std::cout << "Bound wl_output: " << name << std::endl;
    } else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
        self->layer_shell = static_cast<zwlr_layer_shell_v1*>(
            wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, std::min(version, 4u)));
        std::cout << "Bound zwlr_layer_shell_v1" << std::endl;
    } else if (strcmp(interface, wp_viewporter_interface.name) == 0) {
        self->viewporter = static_cast<wp_viewporter*>(
            wl_registry_bind(registry, name, &wp_viewporter_interface, std::min(version, 1u)));
        std::cout << "Bound wp_viewporter" << std::endl;
    } 
}

void InteractiveWallpaper::registry_global_remove(void* data, wl_registry* /*registry*/,
                                                 uint32_t name) {
    InteractiveWallpaper* self = static_cast<InteractiveWallpaper*>(data);

    auto it = self->outputs.begin();
    while (it != self->outputs.end()) {
        if (wl_proxy_get_id(reinterpret_cast<wl_proxy*>(it->first)) == name) {
            std::cout << "Output removed: " << it->second->name << std::endl;
            it = self->outputs.erase(it);
        } else {
            ++it;
        }
    }
}

// Output event handlers
void InteractiveWallpaper::output_geometry(void* data, wl_output* /*wl_output*/,
                                          int32_t /*x*/, int32_t /*y*/, int32_t /*width_mm*/, int32_t /*height_mm*/,
                                          int32_t /*subpixel*/, const char* make, const char* model,
                                          int32_t /*transform*/) {
    Output* output = static_cast<Output*>(data);
    std::cout << "Output geometry: " << (make ? make : "") << " " << (model ? model : "") << std::endl;
}

void InteractiveWallpaper::output_mode(void* data, wl_output* /*wl_output*/, uint32_t flags,
                                      int32_t width, int32_t height, int32_t /*refresh*/) {
    Output* output = static_cast<Output*>(data);
    std::cout << "Output mode: " << width << "x" << height << " flags=" << flags 
              << " (current=" << (flags & WL_OUTPUT_MODE_CURRENT) << ")" << std::endl;
    
    if (width > 0 && height > 0) {
        output->width = static_cast<uint32_t>(width);
        output->height = static_cast<uint32_t>(height);
    }
}

void InteractiveWallpaper::output_done(void* data, wl_output* /*wl_output*/) {
    Output* output = static_cast<Output*>(data);
    std::cout << "Output done: " << output->name << " (" << output->identifier << ")" 
              << " size=" << output->width << "x" << output->height << std::endl;
    
    if (output->parent && output->width > 0 && output->height > 0) {
        output->parent->create_layer_surface(output);
    } else {
        std::cerr << "Cannot create layer surface: invalid output parameters" << std::endl;
    }
}

void InteractiveWallpaper::output_scale(void* data, wl_output* /*wl_output*/, int32_t scale) {
    Output* output = static_cast<Output*>(data);
    output->scale = scale;
    std::cout << "Output scale: " << scale << std::endl;
}

void InteractiveWallpaper::output_name(void* data, wl_output* /*wl_output*/, const char* name) {
    Output* output = static_cast<Output*>(data);
    if (name) output->name = name;
    std::cout << "Output name: " << (name ? name : "") << std::endl;
}

void InteractiveWallpaper::output_description(void* data, wl_output* /*wl_output*/,
                                             const char* description) {
    Output* output = static_cast<Output*>(data);
    if (description) output->identifier = description;
    std::cout << "Output description: " << (description ? description : "") << std::endl;
}

// Layer surface handlers
void InteractiveWallpaper::layer_surface_configure(void* data,
                                                  zwlr_layer_surface_v1* surface,
                                                  uint32_t serial, uint32_t width, uint32_t height) {
    Output* output = static_cast<Output*>(data);
    
    std::cout << "Layer surface configure: " << width << "x" << height << " (serial: " << serial << ")" << std::endl;
    
    output->width = width;
    output->height = height;
    output->configure_serial = serial;
    output->configured = true;

    // Acknowledge the configure
    zwlr_layer_surface_v1_ack_configure(surface, serial);

    // Создаем поверхность только если ее еще нет.
    if (output->egl_surface == EGL_NO_SURFACE) {
        output->parent->create_egl_surface(output);

        // Инициализируем эффект только после первого создания поверхности
        if (output->effect && output->egl_surface != EGL_NO_SURFACE) {
            if (eglMakeCurrent(output->parent->egl_display, output->egl_surface, output->egl_surface, output->parent->egl_context)) {
                // Проверяем результат инициализации
                if (output->effect->initialize(width, height)) {
                    std::cout << "Effect initialized for output: " << output->name << std::endl;
                } else {
                    std::cerr << "ERROR: Failed to initialize effect for output: " << output->name << ". Disabling it." << std::endl;
                    // Если инициализация провалилась, сбрасываем эффект, чтобы не было падения
                    output->effect.reset();
                }
            } else {
                std::cerr << "Failed to make EGL context current for effect initialization" << std::endl;
            }
        }

    } else {
        // Если поверхность уже есть, просто меняем размер окна
        if (output->egl_window) {
            wl_egl_window_resize(output->egl_window, width, height, 0, 0);
            std::cout << "Resized EGL window for output: " << output->name << std::endl;
        }
    }

    // Commit the surface to make it visible
    if (output->surface) {
        wl_surface_commit(output->surface);
    }
}


void InteractiveWallpaper::layer_surface_closed(void* data,
                                               zwlr_layer_surface_v1* /*surface*/) {
    Output* output = static_cast<Output*>(data);
    std::cout << "Layer surface closed for output: " << output->name << std::endl;

    if (output->parent) {
        output->parent->outputs.erase(output->output_obj);
    }
}

// Create layer surface for output
void InteractiveWallpaper::create_layer_surface(Output* output) {
    if (!output || !output->parent) {
        std::cerr << "Invalid output or parent in create_layer_surface" << std::endl;
        return;
    }

    if (!output->parent->compositor || !output->parent->layer_shell) {
        std::cerr << "Compositor or layer shell not available" << std::endl;
        return;
    }

    // Create surface if it doesn't exist
    if (!output->surface) {
        output->surface = wl_compositor_create_surface(output->parent->compositor);
        if (!output->surface) {
            std::cerr << "Failed to create wl_surface" << std::endl;
            return;
        }
    }

    // Create layer surface
    output->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        output->parent->layer_shell, output->surface, output->output_obj,
        ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND, "wallpaper");

    // временная проверка — позволяет понять, получает ли поверхность ввод
    //output->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
    //output->parent->layer_shell, output->surface, output->output_obj,
    //ZWLR_LAYER_SHELL_V1_LAYER_TOP, "wallpaper");

    if (!output->layer_surface) {
        std::cerr << "Failed to create layer surface" << std::endl;
        if (output->surface) {
            wl_surface_destroy(output->surface);
            output->surface = nullptr;
        }
        return;
    }

    // Configure layer surface properties
    zwlr_layer_surface_v1_set_size(output->layer_surface, 0, 0);

    zwlr_layer_surface_v1_set_anchor(output->layer_surface,
        ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_exclusive_zone(output->layer_surface, -1);

    zwlr_layer_surface_v1_set_keyboard_interactivity(output->layer_surface, 
        ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);

    // Add listener
    zwlr_layer_surface_v1_add_listener(output->layer_surface,
                                      &layer_surface_listener, output);

    // Commit to apply changes
    wl_surface_commit(output->surface);
    
    std::cout << "Created layer surface for output: " << output->name << std::endl;
}
