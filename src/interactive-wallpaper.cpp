// src/interactive-wallpaper.cpp
#include "interactive-wallpaper.hpp"
#include <glm/glm.hpp>

#include <iostream>
#include <cstring>
#include <algorithm>
#include <unistd.h>
#include <chrono>
#include <cstdlib>
#include <atomic>
#include <sys/signalfd.h> // Для безопасной обработки сигналов через epoll
#include <signal.h>
#include <filesystem>
#include <sys/socket.h> 
#include <sys/un.h>     

extern std::atomic<bool> global_running;

// --- Статические слушатели протоколов Wayland ---
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

// ==============================================================================
// CONSTRUCTOR / DESTRUCTOR
// ==============================================================================

InteractiveWallpaper::InteractiveWallpaper(const WallpaperConfig& cfg, LuaEngine& engine) 
    : config(cfg), lua_engine(engine) 
{
    display = wl_display_connect(nullptr);
    if (!display) {
        std::cerr << "Failed to connect to Wayland display" << std::endl;
        return;
    }
    
    if (!init_egl()) {
        std::cerr << "Failed to initialize EGL" << std::endl;
    }

    epoll_fd = epoll_create1(IN_CLOEXEC);
    if (epoll_fd < 0) {
        std::cerr << "Failed to create epoll fd: " << strerror(errno) << std::endl;
    }

    // [NEW] Учим LuaEngine находить эффект для конкретного монитора
    lua_engine.get_effect_for_output = [this](const std::string& name) -> IWallpaperEffectABI* {
        for (auto& pair : outputs) {
            if (pair.second->name == name) {
                return pair.second->effect.get();
            }
        }
        return nullptr;
    };
}

InteractiveWallpaper::~InteractiveWallpaper() {
    outputs.clear();

    if (egl_context != EGL_NO_CONTEXT) {
        eglDestroyContext(egl_display, egl_context);
        egl_context = EGL_NO_CONTEXT;
    }
    if (egl_display != EGL_NO_DISPLAY) {
        eglTerminate(egl_display);
        egl_display = EGL_NO_DISPLAY;
    }

    if (layer_shell) zwlr_layer_shell_v1_destroy(layer_shell);
    if (compositor) wl_compositor_destroy(compositor);
    if (shm) wl_shm_destroy(shm);
    if (viewporter) wp_viewporter_destroy(viewporter);

    if (display) {
        wl_display_disconnect(display);
        display = nullptr;
    }

    if (epoll_fd >= 0) close(epoll_fd);
    if (inotify_fd >= 0) close(inotify_fd);
}

// ==============================================================================
// ICoreContextABI IMPLEMENTATION
// ==============================================================================

IBlackBoardABI* InteractiveWallpaper::get_blackboard() {
    return &blackboard;
}

void InteractiveWallpaper::register_epoll_fd(int fd, void (*callback)(uint32_t, void*), void* user_data) {
    register_epoll_fd_cxx(fd, [callback, user_data](uint32_t events) {
        if (callback) callback(events, user_data);
    });
}

void InteractiveWallpaper::unregister_epoll_fd(int fd) {
    if (fd < 0 || epoll_fd < 0) return;
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
    epoll_callbacks.erase(fd);
}

void InteractiveWallpaper::register_epoll_fd_cxx(int fd, std::function<void(uint32_t)> callback) {
    if (fd < 0 || epoll_fd < 0 || !callback) return;
    struct epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev) == 0) {
        epoll_callbacks[fd] = std::move(callback);
    } else {
        std::cerr << "Failed to add fd " << fd << " to epoll: " << strerror(errno) << std::endl;
    }
}

// ==============================================================================
// CONFIGURATION (Lua) & INOTIFY (Hot-Reloading)
// ==============================================================================

// [UPDATED] Применение настроек к конкретному монитору с учетом локальных переопределений
void InteractiveWallpaper::apply_config_to_effect(Output* output) {
    if (!output || !output->effect) return;
    
    // Запрашиваем конфиг именно для этого монитора (по имени wl_output или описанию)
    OutputConfig out_cfg = lua_engine.get_output_config(output->name, output->identifier);
    std::string target_effect = out_cfg.effect_name.empty() ? default_effect_name_ : out_cfg.effect_name;

    // Применяем параметры с каскадным слиянием (глобальные дефолты + переопределения экрана)
    lua_engine.apply_effect_settings(output->effect.get(), target_effect, out_cfg.custom_settings);
}

void InteractiveWallpaper::apply_config_to_all_outputs() {
    for (auto& pair : outputs) {
        apply_config_to_effect(pair.second.get());
    }
}

const char* InteractiveWallpaper::get_bundle_path(const char* plugin_name) {
    if (!plugin_manager_ || !plugin_name) return "";
    // Возвращаем указатель на внутреннюю строку из map (это безопасно, пока жив PluginManager)
    static std::string last_queried_path; 
    last_queried_path = plugin_manager_->get_bundle_path(plugin_name);
    return last_queried_path.c_str();
}

void InteractiveWallpaper::setup_inotify() {
    inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (inotify_fd < 0) return;
    
    const char* xdg_config = std::getenv("XDG_CONFIG_HOME");
    std::string config_dir = xdg_config ? std::string(xdg_config) + "/interactive-wallpaper" 
                                        : std::string(std::getenv("HOME")) + "/.config/interactive-wallpaper";

    // 1. Отслеживаем изменения в основной папке (init.lua) и сгенерированных конфигах
    inotify_add_watch(inotify_fd, config_dir.c_str(), IN_MODIFY | IN_CLOSE_WRITE | IN_MOVED_TO);
    inotify_add_watch(inotify_fd, (config_dir + "/plugins").c_str(), IN_MODIFY | IN_CLOSE_WRITE | IN_MOVED_TO);

    // 2. Отслеживаем ВСЕ папки бандлов (и шейдеры, и пресеты) через симлинк /effects
    std::error_code ec;
    // ВАЖНО: follow_directory_symlink заставит inotify зайти внутрь симлинка effects -> plugins
    auto options = std::filesystem::directory_options::follow_directory_symlink | std::filesystem::directory_options::skip_permission_denied;
    
    std::string effects_dir = config_dir + "/effects";
    if (std::filesystem::exists(effects_dir, ec)) {
        inotify_add_watch(inotify_fd, effects_dir.c_str(), IN_MODIFY | IN_CLOSE_WRITE | IN_MOVED_TO);
        
        for (const auto& entry : std::filesystem::recursive_directory_iterator(effects_dir, options, ec)) {
            if (entry.is_directory(ec)) {
                inotify_add_watch(inotify_fd, entry.path().c_str(), IN_MODIFY | IN_CLOSE_WRITE | IN_MOVED_TO);
            }
        }
    }

    register_epoll_fd_cxx(inotify_fd, [this](uint32_t) { this->process_inotify_events(); });
    apply_config_to_all_outputs();
}

void InteractiveWallpaper::setup_ipc() {
    ipc_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (ipc_fd < 0) return;

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    const char* socket_path = "/run/user/1000/shader-desk.sock"; // В продакшене брать getuid() или XDG_RUNTIME_DIR
    
    unlink(socket_path); // Удаляем старый сокет, если остался от прошлого запуска
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    if (bind(ipc_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0 || listen(ipc_fd, 5) < 0) {
        close(ipc_fd);
        ipc_fd = -1;
        return;
    }

    // Регистрируем слушающий сокет в твоем epoll-цикле с нулевой задержкой!
    register_epoll_fd_cxx(ipc_fd, [this](uint32_t) {
        int client_fd = accept4(ipc_fd, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (client_fd < 0) return;

        // Читаем команду от клиента (напр., "core.outputs['eDP-1'].settings.bloom_intensity = 0.8")
        char buffer[1024]{0};
        ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
        
        if (bytes_read > 0) {
            std::string cmd(buffer);
            std::string response = "OK\n";

            try {
                // Выполняем пришедшую строку прямо в твоем Lua-движке!
                sol::protected_function_result result = lua_engine.get_state().script(cmd);
                if (!result.valid()) {
                    sol::error err = result;
                    response = std::string("ERROR: ") + err.what() + "\n";
                }
            } catch (const std::exception& e) {
                response = std::string("ERROR: ") + e.what() + "\n";
            }

            // Отправляем ответ клиенту
            write(client_fd, response.c_str(), response.length());
        }
        close(client_fd);
    });
}

// [UPDATED] Обработчик горячей перезагрузки (Мультимониторный)
void InteractiveWallpaper::process_inotify_events() {
    char buf[4096] __attribute__ ((aligned(__alignof__(struct inotify_event))));
    bool lua_modified = false;
    bool shader_modified = false;
    
    while (true) {
        ssize_t len = read(inotify_fd, buf, sizeof(buf));
        if (len <= 0) break;

        const struct inotify_event *event;
        for (char *ptr = buf; ptr < buf + len; ptr += sizeof(struct inotify_event) + event->len) {
            event = (const struct inotify_event *) ptr;
            if (event->len > 0) {
                std::string name(event->name);
                if (name.find(".lua") != std::string::npos) {
                    lua_modified = true;
                }
                else if (name.find(".glsl") != std::string::npos || name.find(".vert") != std::string::npos || name.find(".frag") != std::string::npos) {
                    shader_modified = true;
                }
            }
        }
    }

    if (lua_modified) {
        std::cout << "[Hot-Reload] Lua configuration changed..." << std::endl;
        if (lua_engine.reload()) {
            // При перезагрузке Lua просто вызываем apply_effect_to_output для каждого экрана.
            // Метод сам определит, сменился ли эффект на этом экране, или нужно только обновить параметры.
            for (auto& pair : outputs) {
                apply_effect_to_output(pair.second.get());
            }
            // Переконфигурируем провайдеры данных
            if (plugin_manager_) {
                plugin_manager_->initialize_providers(this, [this](IDataProviderABI* p) {
                    return lua_engine.configure_provider(p);
                });
            }
        }
    }

    if (shader_modified) {
        std::cout << "[Hot-Reload] Shader file changed. Recompiling graphics pipeline..." << std::endl;
        for (auto& pair : outputs) {
            Output* out = pair.second.get();
            if (out->effect && out->egl_surface != EGL_NO_SURFACE) {
                if (eglMakeCurrent(egl_display, out->egl_surface, out->egl_surface, egl_context)) {
                    out->effect->cleanup();
                    out->effect->initialize(this, out->width, out->height);
                    apply_config_to_effect(out);
                }
            }
        }
    }
}

// ==============================================================================
// EGL AND SYSTEM INITIALIZATION
// ==============================================================================

bool InteractiveWallpaper::init_egl() {
    egl_display = eglGetDisplay((EGLNativeDisplayType)display);
    if (egl_display == EGL_NO_DISPLAY) return false;

    EGLint major, minor;
    if (!eglInitialize(egl_display, &major, &minor)) return false;

    const EGLint config_attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 24, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_NONE
    };

    EGLint num_configs;
    if (!eglChooseConfig(egl_display, config_attribs, &egl_config, 1, &num_configs) || num_configs == 0) {
        return false;
    }

    const EGLint context_attribs[] = { EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 0, EGL_NONE };
    egl_context = eglCreateContext(egl_display, egl_config, EGL_NO_CONTEXT, context_attribs);
    
    if (egl_context == EGL_NO_CONTEXT) {
        const EGLint context_attribs_es2[] = { EGL_CONTEXT_MAJOR_VERSION, 2, EGL_NONE };
        egl_context = eglCreateContext(egl_display, egl_config, EGL_NO_CONTEXT, context_attribs_es2);
        if (egl_context == EGL_NO_CONTEXT) return false;
    }

    return true;
}

void InteractiveWallpaper::create_egl_surface(Output* output) {
    if (!output || !output->surface || output->width == 0 || output->height == 0) return;

    output->egl_window = wl_egl_window_create(output->surface, output->width, output->height);
    output->egl_surface = eglCreateWindowSurface(egl_display, egl_config, (EGLNativeWindowType)output->egl_window, nullptr);
    
    if (output->egl_surface == EGL_NO_SURFACE) {
        wl_egl_window_destroy(output->egl_window);
        output->egl_window = nullptr;
    }
}

bool InteractiveWallpaper::initialize() {
    if (!display || epoll_fd < 0) return false;

    struct wl_registry* registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, this);
    wl_display_roundtrip(display);
    wl_registry_destroy(registry);

    if (!compositor || !layer_shell) return false;

    int wl_fd = wl_display_get_fd(display);
    struct epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = wl_fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, wl_fd, &ev);

    setup_inotify();
    setup_ipc();

    // Безопасный перехват сигналов завершения через epoll
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);

    int sig_fd = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
    if (sig_fd >= 0) {
        register_epoll_fd_cxx(sig_fd, [this, sig_fd](uint32_t) {
            struct signalfd_siginfo fdsi;
            while (read(sig_fd, &fdsi, sizeof(fdsi)) > 0) {
                std::cout << "\nSignal " << fdsi.ssi_signo << " received. Shutting down gracefully..." << std::endl;
                global_running = false;
                this->stop();
            }
        });
    } else {
        std::cerr << "Failed to create signalfd: " << strerror(errno) << std::endl;
    }
    
    std::cout << "InteractiveWallpaper Microkernel initialized successfully." << std::endl;
    return true;
}

// ==============================================================================
// MAIN EVENT LOOP (ZERO-LATENCY EPOLL BASED)
// ==============================================================================

void InteractiveWallpaper::run() {
    if (!display) return;
    std::cout << "Starting main loop (epoll based)..." << std::endl;
    wl_display_roundtrip(display);

    const int MAX_EVENTS = 16;
    struct epoll_event events[MAX_EVENTS];
    int wl_fd = wl_display_get_fd(display);

    while (global_running.load() && running) {
        while (wl_display_prepare_read(display) != 0) {
            wl_display_dispatch_pending(display);
        }
        wl_display_flush(display);

        int n_events = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);

        if (n_events < 0) {
            wl_display_cancel_read(display);
            if (errno == EINTR) continue;
            std::cerr << "epoll_wait error: " << strerror(errno) << std::endl;
            break;
        }

        bool wayland_event = false;

        for (int i = 0; i < n_events; ++i) {
            int fd = events[i].data.fd;
            uint32_t revents = events[i].events;

            if (fd == wl_fd) {
                wayland_event = true;
            } else if (epoll_callbacks.find(fd) != epoll_callbacks.end()) {
                epoll_callbacks[fd](revents);
            }
        }

        if (wayland_event) {
            wl_display_read_events(display);
        } else {
            wl_display_cancel_read(display);
        }
        wl_display_dispatch_pending(display);
    }
}

void InteractiveWallpaper::stop() { running = false; }

// ==============================================================================
// WAYLAND AND EFFECT RENDERING
// ==============================================================================

void InteractiveWallpaper::set_plugin_manager(PluginManager* pm, const std::string& default_effect) {
    plugin_manager_ = pm;
    default_effect_name_ = default_effect;
}

// [UPDATED] Мультимониторный выбор и инициализация эффекта
void InteractiveWallpaper::apply_effect_to_output(Output* output) {
    if (!plugin_manager_) return;
    
    // 1. Узнаем, какой эффект должен работать именно на этом физическом мониторе
    OutputConfig out_cfg = lua_engine.get_output_config(output->name, output->identifier);
    std::string target_effect = out_cfg.effect_name.empty() ? default_effect_name_ : out_cfg.effect_name;

    if (target_effect.empty()) {
        std::cerr << "[Core] No effect specified for output: " << output->name << std::endl;
        return;
    }

    // 2. Если на экране еще нет эффекта ИЛИ в Lua изменили имя эффекта для этого монитора
    if (!output->effect || std::string(output->effect->get_name()) != target_effect) {
        
        // Безопасно уничтожаем старый эффект (активировав его EGL-контекст)
        if (output->effect && output->egl_surface != EGL_NO_SURFACE) {
            eglMakeCurrent(egl_display, output->egl_surface, output->egl_surface, egl_context);
            output->effect->cleanup();
            output->effect.reset();
        }
        
        std::cout << "[Core] Assigning effect '" << target_effect 
                  << "' to Wayland output '" << output->name << "'" << std::endl;
                  
        // Создаем независимый экземпляр C++ плагина специально для этого монитора!
        output->effect = plugin_manager_->create_effect(target_effect);
    }

    // 3. Применяем настройки (общие + переопределения экрана) и инициализируем OpenGL-конвейер
    if (output->effect) {
        apply_config_to_effect(output);
        
        if (output->configured && output->egl_surface != EGL_NO_SURFACE) {
            if (eglMakeCurrent(egl_display, output->egl_surface, output->egl_surface, egl_context)) {
                output->effect->initialize(this, output->width, output->height);
            }
        }
    }
}

static const wl_callback_listener frame_listener = {
    .done = InteractiveWallpaper::frame_handle_done,
};

void InteractiveWallpaper::frame_handle_done(void* data, wl_callback* callback, uint32_t) {
    if (callback) wl_callback_destroy(callback);
    Output* output = static_cast<Output*>(data);
    output->frame_callback = nullptr;
    
    if (output->parent) output->parent->render_output(output);
}

// [UPDATED] Рендеринг кадра с защитой от скачков dt и вызовом покадрового Lua-хука
void InteractiveWallpaper::render_output(Output* output) {
    if (!output->configured || !output->effect || output->egl_surface == EGL_NO_SURFACE) return;

    // 1. Расчет точного Delta Time для текущего монитора
    auto now = std::chrono::steady_clock::now();
    std::chrono::duration<float> delta_duration = now - output->last_frame_time;
    float dt = delta_duration.count();
    output->last_frame_time = now;

    // [FIX]: Защита от Lag-Spike и первого кадра.
    // Если dt > 0.1 сек (падение ниже 10 FPS или инициализация Wayland), 
    // нормализуем до 60 Гц (16.6 мс), чтобы анимации в Lua не совершали скачков.
    if (dt > 0.1f || dt < 0.0f) {
        dt = 0.0166f;
    }

    // 2. Активация EGL-контекста текущего экрана
    if (eglMakeCurrent(egl_display, output->egl_surface, output->egl_surface, egl_context)) {
        
        // 3. Вызов покадрового хука Lua ДО рендера шейдера!
        // Скрипт в Lua прочитает аудио/мышку из BlackBoard, произведет расчеты
        // и запишет новые значения обратно, которые шейдер подхватит в render().
        lua_engine.on_frame(dt, output->name);

        // 4. Отрисовка визуального плагина
        output->effect->render(output->width, output->height);
        
        // 5. Запрос следующего кадра у композитора Wayland
        output->frame_callback = wl_surface_frame(output->surface);
        wl_callback_add_listener(output->frame_callback, &frame_listener, output);
        
        // 6. Отправка буфера на экран (VSync)
        eglSwapBuffers(egl_display, output->egl_surface);
    }
}

// ==============================================================================
// WAYLAND PROTOCOLS (STANDARD BOILERPLATE LOGIC)
// ==============================================================================

void InteractiveWallpaper::registry_global(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
    auto* self = static_cast<InteractiveWallpaper*>(data);
    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        self->compositor = static_cast<wl_compositor*>(wl_registry_bind(registry, name, &wl_compositor_interface, std::min(version, 4u)));
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        self->shm = static_cast<wl_shm*>(wl_registry_bind(registry, name, &wl_shm_interface, std::min(version, 1u)));
    } else if (strcmp(interface, wl_output_interface.name) == 0) {
        auto output = std::make_unique<Output>();
        output->parent = self;
        output->output_obj = static_cast<wl_output*>(wl_registry_bind(registry, name, &wl_output_interface, std::min(version, 4u)));
        wl_output_add_listener(output->output_obj, &output_listener, output.get());
        self->outputs[output->output_obj] = std::move(output);
    } else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
        self->layer_shell = static_cast<zwlr_layer_shell_v1*>(wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, std::min(version, 4u)));
    } else if (strcmp(interface, wp_viewporter_interface.name) == 0) {
        self->viewporter = static_cast<wp_viewporter*>(wl_registry_bind(registry, name, &wp_viewporter_interface, std::min(version, 1u)));
    } 
}

void InteractiveWallpaper::registry_global_remove(void* data, wl_registry*, uint32_t name) {
    auto* self = static_cast<InteractiveWallpaper*>(data);
    for (auto it = self->outputs.begin(); it != self->outputs.end(); ) {
        if (wl_proxy_get_id(reinterpret_cast<wl_proxy*>(it->first)) == name) {
            it = self->outputs.erase(it);
        } else {
            ++it;
        }
    }
}

void InteractiveWallpaper::output_geometry(void*, wl_output*, int32_t, int32_t, int32_t, int32_t, int32_t, const char*, const char*, int32_t) {}
void InteractiveWallpaper::output_scale(void* data, wl_output*, int32_t scale) { static_cast<Output*>(data)->scale = scale; }
void InteractiveWallpaper::output_name(void* data, wl_output*, const char* name) { if (name) static_cast<Output*>(data)->name = name; }
void InteractiveWallpaper::output_description(void* data, wl_output*, const char* desc) { if (desc) static_cast<Output*>(data)->identifier = desc; }

void InteractiveWallpaper::output_mode(void* data, wl_output*, uint32_t flags, int32_t width, int32_t height, int32_t) {
    Output* output = static_cast<Output*>(data);
    if (width > 0 && height > 0) {
        output->width = static_cast<uint32_t>(width);
        output->height = static_cast<uint32_t>(height);
    }
}

void InteractiveWallpaper::output_done(void* data, wl_output*) {
    Output* output = static_cast<Output*>(data);
    if (output->parent && output->width > 0 && output->height > 0 && !output->layer_surface) {
        output->parent->create_layer_surface(output);
        output->parent->apply_effect_to_output(output);
    }
}

void InteractiveWallpaper::create_layer_surface(Output* output) {
    if (!output->surface) {
        output->surface = wl_compositor_create_surface(compositor);
    }
    output->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        layer_shell, output->surface, output->output_obj,
        ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND, "wallpaper");

    zwlr_layer_surface_v1_set_size(output->layer_surface, 0, 0);
    zwlr_layer_surface_v1_set_anchor(output->layer_surface, 15); // All anchors (1|2|4|8)
    zwlr_layer_surface_v1_set_exclusive_zone(output->layer_surface, -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(output->layer_surface, 0);

    zwlr_layer_surface_v1_add_listener(output->layer_surface, &layer_surface_listener, output);
    wl_surface_commit(output->surface);
}

void InteractiveWallpaper::layer_surface_configure(void* data, zwlr_layer_surface_v1* surface, uint32_t serial, uint32_t width, uint32_t height) {
    Output* output = static_cast<Output*>(data);
    if (width == 0 || height == 0) return;

    output->width = width;
    output->height = height;
    output->configure_serial = serial;
    output->configured = true;

    zwlr_layer_surface_v1_ack_configure(surface, serial);

    if (output->egl_surface == EGL_NO_SURFACE) {
        output->parent->create_egl_surface(output);

        if (output->effect && output->egl_surface != EGL_NO_SURFACE) {
            if (eglMakeCurrent(output->parent->egl_display, output->egl_surface, output->egl_surface, output->parent->egl_context)) {
                if (!output->effect->initialize(output->parent, width, height)) {
                    output->effect.reset();
                }
            }
        }
        if (!output->frame_callback) output->parent->render_output(output);
    } else {
        if (output->egl_window) wl_egl_window_resize(output->egl_window, width, height, 0, 0);
        
        if (output->frame_callback) {
            wl_callback_destroy(output->frame_callback);
            output->frame_callback = nullptr;
        }
        output->parent->render_output(output);
    }
}

void InteractiveWallpaper::layer_surface_closed(void* data, zwlr_layer_surface_v1*) {
    Output* output = static_cast<Output*>(data);
    if (output->parent) output->parent->outputs.erase(output->output_obj);
}
