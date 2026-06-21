// interactive-wallpaper.cpp
#include "interactive-wallpaper.hpp"
//#include "../plugins/ico-sphere-effect/ico-sphere-effect.hpp"
#include <glm/glm.hpp>

#include <iostream>
#include <cstring>
#include <algorithm>
#include <sys/select.h>
#include <unistd.h>

#include <chrono>
#include <fstream>
#include <sys/stat.h>
#include <cstdlib>

#include <sys/epoll.h>
#include <sys/inotify.h>

#include <nlohmann/json.hpp>
#include "utils.hpp"

#include <atomic>

#include <poll.h>

using WallpaperEffectPtr = std::unique_ptr<WallpaperEffect, void(*)(WallpaperEffect*)>;
extern std::atomic<bool> global_running;

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
    if (!audio_client_->connect(get_default_audio_socket_path())) {
        std::cerr << "Warning: Could not connect to audio daemon." << std::endl;
    }



    mouse_sensitivity = 0.05f;
    touchpad_sensitivity = 20.0f;
    
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
    // 1. Отключаем IPC клиенты
    if (audio_client_) {
        audio_client_->disconnect();
    }
    pointer_daemon.disconnect();

    // 2. ВАЖНО: Сначала очищаем все экраны!
    // При вызове clear() сработает деструктор ~Output, который уничтожит 
    // EGL-поверхности, wl_surface и другие Wayland-объекты.
    // Это ОБЯЗАТЕЛЬНО нужно сделать до закрытия EGL контекста и wl_display.
    outputs.clear();

    // 3. Теперь безопасно завершаем EGL
    if (egl_context != EGL_NO_CONTEXT) {
        eglDestroyContext(egl_display, egl_context);
        egl_context = EGL_NO_CONTEXT;
    }
    
    if (egl_display != EGL_NO_DISPLAY) {
        eglTerminate(egl_display);
        egl_display = EGL_NO_DISPLAY;
    }

    // 4. Уничтожаем глобальные объекты Wayland
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

    // 5. Отключаемся от дисплея Wayland
    if (display) {
        wl_display_disconnect(display);
        display = nullptr;
    }

    // 6. Закрываем файловые дескрипторы мультиплексора
    if (epoll_fd >= 0) close(epoll_fd);
    if (inotify_fd >= 0) close(inotify_fd);
}

void InteractiveWallpaper::handle_audio_data(const AudioData& data) {
    for (auto& pair : outputs) {
        if (pair.second && pair.second->effect) {
            pair.second->effect->handle_audio_data(data); // Прямой вызов
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
    bool is_touchpad = normalized;
    float sensitivity = is_touchpad ? touchpad_sensitivity : mouse_sensitivity;
    
    double effective_dx, effective_dy;
    if (std::abs(vx) > 1e-6 && std::abs(dt) > 1e-6) {
        effective_dx = vx * sensitivity * dt;
        effective_dy = vy * sensitivity * dt;
    } else {
        effective_dx = dx * sensitivity;
        effective_dy = dy * sensitivity;
    }

    // Прямой вызов
    for (auto& pair : outputs) {
        if (pair.second && pair.second->effect) {
            pair.second->effect->handle_pointer_motion(effective_dx, effective_dy, is_touchpad);
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

        current_config = std::move(new_config);

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

// ------------------------- Inotify Configuration -------------------------

void InteractiveWallpaper::setup_inotify() {
    // Инициализируем inotify в неблокирующем режиме
    inotify_fd = inotify_init1(IN_NONBLOCK);
    if (inotify_fd < 0) {
        std::cerr << "Failed to init inotify: " << strerror(errno) << std::endl;
        return;
    }
    
    std::string config_path = get_default_config_path();
    std::string config_dir = config_path.substr(0, config_path.find_last_of('/'));
    // Наблюдаем только за изменением файла конфигурации
    //inotify_wd = inotify_add_watch(inotify_fd, config_path.c_str(), IN_MODIFY);
    inotify_wd = inotify_add_watch(inotify_fd, config_dir.c_str(), IN_MODIFY | IN_CLOSE_WRITE | IN_MOVED_TO);

    
    // Выполняем первичную загрузку конфигурации
    if (reload_config()) {
        apply_config_to_all_outputs();
    }
}

void InteractiveWallpaper::process_inotify_events() {
    if (inotify_fd < 0) return;

    // Буфер для событий inotify (с правильным выравниванием памяти)
    char buf[4096] __attribute__ ((aligned(__alignof__(struct inotify_event))));
    
    while (true) {
        ssize_t len = read(inotify_fd, buf, sizeof(buf));
        if (len <= 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break; // Данных больше нет
            std::cerr << "Inotify read error: " << strerror(errno) << std::endl;
            break;
        }

        bool config_modified = false;
        const struct inotify_event *event;
        
        // Перебираем все полученные события
        for (char *ptr = buf; ptr < buf + len; ptr += sizeof(struct inotify_event) + event->len) {
            event = (const struct inotify_event *) ptr;
            if (event->len > 0 && std::string(event->name) == "config.json") {
                config_modified = true;
            }

        }

        // Если конфиг изменился, перезагружаем его и применяем
        if (config_modified) {
            std::cout << "Config file modified, reloading..." << std::endl;
            if (reload_config()) {
                apply_config_to_all_outputs();
            }
        }
    }
}

void InteractiveWallpaper::apply_config_to_all_outputs() {
    // Безопасно применяем настройки ко всем активным экранам (эффектам)
    for (auto& pair : outputs) {
        if (pair.second->effect) {
            apply_config_to_effect(pair.second.get());
        }
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

    // 1. Настраиваем наблюдение за файлом конфигурации
    setup_inotify();

    // 2. Создаем epoll дескриптор
    epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        std::cerr << "Failed to create epoll fd: " << strerror(errno) << std::endl;
        return;
    }

    // Хелпер для добавления дескрипторов в epoll
    auto add_to_epoll = [&](int fd) {
        if (fd < 0) return;
        struct epoll_event ev{};
        ev.events = EPOLLIN;
        ev.data.fd = fd;
        epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev);
    };

    // 3. Добавляем все источники событий в epoll
    add_to_epoll(wl_display_get_fd(display));
    add_to_epoll(audio_client_->get_fd());
    add_to_epoll(pointer_daemon.get_fd());
    add_to_epoll(inotify_fd);

    std::cout << "Starting main loop (epoll based)..." << std::endl;
    wl_display_roundtrip(display); // Убеждаемся, что Wayland инициализирован

    const int MAX_EVENTS = 10;
    struct epoll_event events[MAX_EVENTS];

    while (global_running && running) {
        // Подготавливаем Wayland (сбрасываем исходящий буфер запросов)
        while (wl_display_prepare_read(display) != 0) {
            wl_display_dispatch_pending(display);
        }
        wl_display_flush(display);

        // Ждем событий от ОС. 
        // 16 миллисекунд таймаут — временное решение для 60 FPS,
        // пока мы не внедрили wl_surface_frame (VSync).
        int n_events = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);

        if (n_events < 0) {
            wl_display_cancel_read(display);
            if (errno == EINTR) continue; // Прерывание сигналом (например, Ctrl+C)
            std::cerr << "epoll_wait error: " << strerror(errno) << std::endl;
            break;
        }

        if (n_events == 0) {
            // Таймаут. Отменяем чтение Wayland
            wl_display_cancel_read(display);
        } else {
            bool wayland_event = false;

            // Распределяем полученные события по их обработчикам
            for (int i = 0; i < n_events; ++i) {
                int fd = events[i].data.fd;

                if (fd == wl_display_get_fd(display)) {
                    wayland_event = true;
                } else if (fd == audio_client_->get_fd()) {
                    audio_client_->process_pending_data();
                } else if (fd == pointer_daemon.get_fd()) {
                    pointer_daemon.process_pending_data();
                } else if (fd == inotify_fd) {
                    process_inotify_events();
                }
            }

            // Если были события Wayland - читаем их, иначе отменяем подготовку
            if (wayland_event) {
                wl_display_read_events(display);
            } else {
                wl_display_cancel_read(display);
            }
            
            // Выполняем Wayland-коллбэки
            wl_display_dispatch_pending(display);
        }

        
    }

    std::cout << "Main loop exited" << std::endl;
}

void InteractiveWallpaper::stop() {
    running = false;
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
        
        // ВАЖНО: Проверяем, не создали ли мы уже поверхность для этого монитора!
        if (!output->layer_surface) {
            // Создаем поверхность
            output->parent->create_layer_surface(output);
            // СОЗДАЕМ И ПРИВЯЗЫВАЕМ ЭФФЕКТ К ЭТОМУ МОНИТОРУ
            output->parent->apply_effect_to_output(output);
        }
        
    } else {
        std::cerr << "Cannot create layer surface: invalid output parameters" << std::endl;
    }
}

// Добавь реализацию листенера (где-нибудь рядом с другими листенерами)
static const wl_callback_listener frame_listener = {
    .done = InteractiveWallpaper::frame_handle_done,
};

void InteractiveWallpaper::frame_handle_done(void* data, wl_callback* callback, uint32_t /*time*/) {
    if (callback) {
        wl_callback_destroy(callback);
    }
    Output* output = static_cast<Output*>(data);
    output->frame_callback = nullptr;
    
    // Запускаем отрисовку следующего кадра
    if (output->parent) {
        output->parent->render_output(output);
    }
}

void InteractiveWallpaper::render_output(Output* output) {
    if (!output->configured || !output->effect || output->egl_surface == EGL_NO_SURFACE) return;

    if (eglMakeCurrent(egl_display, output->egl_surface, output->egl_surface, egl_context)) {
        // Отрисовываем кадр
        output->effect->render(output->width, output->height);
        
        // ВАЖНО: Запрашиваем у Wayland коллбек на следующий кадр ПЕРЕД eglSwapBuffers
        output->frame_callback = wl_surface_frame(output->surface);
        wl_callback_add_listener(output->frame_callback, &frame_listener, output);
        
        // Отправляем кадр на экран
        eglSwapBuffers(egl_display, output->egl_surface);
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
    
    // Защита от нулевых размеров (иногда композитор так просит клиента выбрать размер самому)
    if (width == 0 || height == 0) return;

    std::cout << "Layer surface configure: " << width << "x" << height << " (serial: " << serial << ")" << std::endl;
    
    output->width = width;
    output->height = height;
    output->configure_serial = serial;
    output->configured = true;

    // 1. Обязательно подтверждаем композитору, что приняли новые размеры
    zwlr_layer_surface_v1_ack_configure(surface, serial);

    // 2. Если EGL-поверхность еще не создана — создаем
    if (output->egl_surface == EGL_NO_SURFACE) {
        output->parent->create_egl_surface(output);

        if (output->effect && output->egl_surface != EGL_NO_SURFACE) {
            if (eglMakeCurrent(output->parent->egl_display, output->egl_surface, output->egl_surface, output->parent->egl_context)) {
                if (!output->effect->initialize(width, height)) {
                    std::cerr << "ERROR: Failed to initialize effect. Disabling it." << std::endl;
                    output->effect.reset();
                }
            }
        }
        
        // Запускаем отрисовку первого кадра
        if (!output->frame_callback) {
            output->parent->render_output(output);
        }

    } else {
        // 3. ПРАВИЛЬНЫЙ РЕСАЙЗ: Если окно уже есть
        if (output->egl_window) {
            // Сообщаем драйверу EGL о новом размере
            wl_egl_window_resize(output->egl_window, width, height, 0, 0);
            std::cout << "Resized EGL window for output: " << output->name << std::endl;
        }
        
        // ВАЖНО: Wayland требует, чтобы после ack_configure мы немедленно 
        // предоставили буфер с НОВЫМ размером. Иначе будет графический глитч.
        // Поэтому мы отменяем ожидание следующего кадра и рисуем принудительно ПРЯМО СЕЙЧАС.
        if (output->frame_callback) {
            wl_callback_destroy(output->frame_callback);
            output->frame_callback = nullptr;
        }
        output->parent->render_output(output);
    }
}

void InteractiveWallpaper::set_plugin_manager(PluginManager* pm, const std::string& effect_name) {
    plugin_manager_ = pm;
    current_effect_name_ = effect_name;
}

void InteractiveWallpaper::apply_effect_to_output(Output* output) {
    if (!plugin_manager_ || current_effect_name_.empty()) return;
    
    output->effect = plugin_manager_->create_effect(current_effect_name_);
    if (output->effect) {
        apply_config_to_effect(output); // Сразу применяем настройки
        
        if (output->configured && output->egl_surface != EGL_NO_SURFACE) {
            if (eglMakeCurrent(egl_display, output->egl_surface, output->egl_surface, egl_context)) {
                output->effect->initialize(output->width, output->height);
            }
        }
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
