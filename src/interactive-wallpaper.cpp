// src/interactive-wallpaper.cpp
#include "interactive-wallpaper.hpp"
#include <glm/glm.hpp>
#include <cmath>

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
#include "ipc-utils.hpp"  

// Безопасное подключение Tracy: если профилирование выключено в CMake,
#ifdef TRACY_ENABLE
    #include <tracy/Tracy.hpp>
#else
    // Все макросы превращаются в пустышки с 0% накладных расходов процессора
    #define ZoneScoped
    #define ZoneScopedN(name)
    #define ZoneText(txt, size)
    #define FrameMark
    #define FrameMarkNamed(name)
    namespace tracy { inline void SetThreadName(const char*) {} }
#endif

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
                // Ищем первый попавшийся НЕ-постпроцесс слой (обычно это 3D-сфера)
                for (auto it = pair.second->layers.rbegin(); it != pair.second->layers.rend(); ++it) {
                    if (!it->is_postprocess && it->effect) return it->effect.get();
                }
                // Если не нашли — возвращаем просто верхний слой
                if (!pair.second->layers.empty()) return pair.second->layers.back().effect.get();
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

// [UPDATED] Применение настроек ко всем слоям конкретного монитора
void InteractiveWallpaper::apply_config_to_effect(Output* output) {
    if (!output || output->layers.empty()) return;
    
    OutputConfig out_cfg = lua_engine.get_output_config(output->name, output->identifier);
    
    for (size_t i = 0; i < output->layers.size() && i < out_cfg.layers.size(); ++i) {
        auto& layer = output->layers[i];
        if (layer.effect) {
            lua_engine.apply_effect_settings(layer.effect.get(), layer.name, out_cfg.layers[i].custom_settings);
        }
    }
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
    
    // Реагируем только на финализацию записи или атомарную подмену
    uint32_t mask = IN_CLOSE_WRITE | IN_MOVED_TO;

    const char* xdg_config = std::getenv("XDG_CONFIG_HOME");
    std::string config_dir = xdg_config ? std::string(xdg_config) + "/interactive-wallpaper" 
                                        : std::string(std::getenv("HOME")) + "/.config/interactive-wallpaper";

    // 1. Базовые пользовательские директории
    inotify_add_watch(inotify_fd, config_dir.c_str(), mask);
    inotify_add_watch(inotify_fd, (config_dir + "/plugins").c_str(), mask);

    // 2. Вспомогательная лямбда для рекурсивного добавления папок
    std::error_code ec;
    auto options = std::filesystem::directory_options::follow_directory_symlink | std::filesystem::directory_options::skip_permission_denied;
    
    auto watch_dir_tree = [&](const std::string& root_path) {
        if (std::filesystem::exists(root_path, ec)) {
            inotify_add_watch(inotify_fd, root_path.c_str(), mask);
            for (const auto& entry : std::filesystem::recursive_directory_iterator(root_path, options, ec)) {
                if (entry.is_directory(ec)) {
                    inotify_add_watch(inotify_fd, entry.path().c_str(), mask);
                }
            }
        }
    };

    // 3. Добавляем в прослушку все возможные места обитания плагинов и конфигов
    watch_dir_tree(config_dir + "/effects");           // Сценарий 3 (Sandbox моддера)
    watch_dir_tree("./src/defaults");                  // Сценарий 1 (Локальный init.lua)
    watch_dir_tree("./plugins");                       // Сценарий 1 (Исходники шейдеров In-Tree)
    
    // (Опционально) Если CMake копирует шейдеры прямо в build-папку
    watch_dir_tree("./build-release/plugins");         
    watch_dir_tree("./build-tracy/plugins");

    register_epoll_fd_cxx(inotify_fd, [this](uint32_t) { this->process_inotify_events(); });
    apply_config_to_all_outputs();
}

void InteractiveWallpaper::setup_ipc() {
    ipc_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (ipc_fd < 0) return;

    std::string socket_path = shader_desk::get_ipc_socket_path("shader-desk");
    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);
    unlink(socket_path.c_str()); 

    // ЗАЩИТА ПРАВ ДОСТУПА: Только наш пользователь сможет писать в сокет (0600)
    mode_t old_mask = umask(0077); 
    int bind_res = bind(ipc_fd, (struct sockaddr*)&addr, sizeof(addr));
    umask(old_mask); 

    if (bind_res < 0 || listen(ipc_fd, 5) < 0) {
        std::cerr << "Failed to bind IPC socket at " << socket_path << std::endl;
        close(ipc_fd);
        ipc_fd = -1;
        return;
    }

    // Слушаем входящие подключения
    register_epoll_fd_cxx(ipc_fd, [this](uint32_t) {
        int client_fd = accept4(ipc_fd, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (client_fd < 0) return;

        // Регистрируем НОВЫЙ клиентский сокет в epoll для чтения данных
        register_epoll_fd_cxx(client_fd, [this, client_fd](uint32_t) {
            char buffer[1024];
            ssize_t bytes = read(client_fd, buffer, sizeof(buffer));

            if (bytes > 0) {
                ipc_buffers[client_fd].append(buffer, bytes);
                
                // Защита от спама/OOM (если прислали > 8кб без переноса строки)
                if (ipc_buffers[client_fd].size() > 8192) goto close_client;

                // ФРЕЙМИНГ: Выполняем все полные команды, разделенные '\n'
                size_t pos;
                while ((pos = ipc_buffers[client_fd].find('\n')) != std::string::npos) {
                    std::string cmd = ipc_buffers[client_fd].substr(0, pos);
                    ipc_buffers[client_fd].erase(0, pos + 1);

                    // БЕЗОПАСНОЕ ИСПОЛНЕНИЕ (Без C++ Exception, без abort'а)
                    auto result = lua_engine.get_state().safe_script(cmd, sol::script_pass_on_error);
                    
                    if (result.valid()) {
                        write(client_fd, "OK\n", 3);
                    } else {
                        sol::error err = result;
                        std::string resp = std::string("LUA_ERR: ") + err.what() + "\n";
                        write(client_fd, resp.c_str(), resp.length());
                    }
                }
            } else if (bytes == 0 || (bytes < 0 && errno != EAGAIN)) {
            close_client:
                ipc_buffers.erase(client_fd);
                unregister_epoll_fd(client_fd);
                close(client_fd);
            }
        });
    });
}

//  Обработчик горячей перезагрузки (Мультимониторный)
void InteractiveWallpaper::process_inotify_events() {
    char buf[4096] __attribute__ ((aligned(__alignof__(struct inotify_event))));
    bool reload_lua = false;
    bool reload_shader = false;
    
    while (true) {
        ssize_t len = read(inotify_fd, buf, sizeof(buf));
        if (len <= 0) break;

        const struct inotify_event *event;
        for (char *ptr = buf; ptr < buf + len; ptr += sizeof(struct inotify_event) + event->len) {
            event = (const struct inotify_event *) ptr;
            if (event->len > 0) {
                std::string name(event->name);
                
                // Игнорируем скрытые файлы и временные swap-файлы редакторов (.swp, .tmp)
                if (name.empty() || name[0] == '.' || name.back() == '~') continue;

                if (name.find(".lua") != std::string::npos) reload_lua = true;
                else if (name.find(".glsl") != std::string::npos || 
                         name.find(".vert") != std::string::npos || 
                         name.find(".frag") != std::string::npos) reload_shader = true;
            }
        }
    }

    // --- МГНОВЕННЫЙ БЕЗОПАСНЫЙ RELOAD ---

    if (reload_lua) {
        std::cout << "\n\033[36m[Hot-Reload] Validating Lua configuration...\033[0m" << std::endl;
        if (lua_engine.reload()) { // Внутри будет синтаксическая предпроверка!
            for (auto& pair : outputs) {
                apply_effect_to_output(pair.second.get());
            }
            if (plugin_manager_) {
                plugin_manager_->initialize_providers(this, [this](IDataProviderABI* p) {
                    return lua_engine.configure_provider(p);
                });
            }
            std::cout << "\033[32m[Hot-Reload] Lua successfully applied.\033[0m\n" << std::endl;
        }
    }

    if (reload_shader) {
        std::cout << "\n\033[36m[Hot-Reload] Recompiling shaders (Shadow-Commit mode)...\033[0m" << std::endl;
        for (auto& pair : outputs) {
            Output* out = pair.second.get();
            if (out->layers.empty() || out->egl_surface == EGL_NO_SURFACE) continue;

            if (eglMakeCurrent(egl_display, out->egl_surface, out->egl_surface, egl_context)) {
                for (auto& layer : out->layers) {
                    if (!layer.effect) continue;

                    // 1. Создаем теневую (новую) копию эффекта
                    WallpaperEffectPtr shadow_effect = plugin_manager_->create_effect(layer.name);
                    if (!shadow_effect) continue;

                    // 2. Пытаемся инициализировать (компиляция шейдеров происходит здесь)
                    if (shadow_effect->initialize(this, out->fbo_w, out->fbo_h)) {
                        // 3. Успех! Делаем Swap
                        layer.effect = std::move(shadow_effect);
                        std::cout << "  ✓ '" << layer.name << "' recompiled seamlessly." << std::endl;
                    } else {
                        // 4. Ошибка компиляции! 
                        // shadow_effect будет уничтожен при выходе из области видимости,
                        // а layer.effect (старая рабочая версия) продолжит рендериться на экране.
                        std::cerr << "\033[31m  ✗ '" << layer.name << "' compilation failed. Keeping old version.\033[0m" << std::endl;
                    }
                }
                // Заново применяем настройки из Lua (цвета, скорость) к новым инстансам
                apply_config_to_effect(out);
            }
        }
        std::cout << "\033[32m[Hot-Reload] Shader pipeline update finished.\033[0m\n" << std::endl;
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

    // Вычисляем физический размер в пикселях
    uint32_t buffer_width = output->width * output->scale;
    uint32_t buffer_height = output->height * output->scale;

    output->egl_window = wl_egl_window_create(output->surface, buffer_width, buffer_height);
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
    tracy::SetThreadName("Wayland Event Loop"); // Имя потока в профайлере
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
        FrameMark;
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

// [NEW] Управление памятью FBO
void InteractiveWallpaper::Output::destroy_fbos() {
    if (fbo[0]) { glDeleteFramebuffers(2, fbo); fbo[0] = fbo[1] = 0; }
    if (tex[0]) { glDeleteTextures(2, tex); tex[0] = tex[1] = 0; }
    if (depth_rbo[0]) { glDeleteRenderbuffers(2, depth_rbo); depth_rbo[0] = depth_rbo[1] = 0; }
    if (fbo_feedback) { glDeleteFramebuffers(1, &fbo_feedback); fbo_feedback = 0; }
    if (tex_feedback) { glDeleteTextures(1, &tex_feedback); tex_feedback = 0; }
}

void InteractiveWallpaper::Output::allocate_fbos(uint32_t w, uint32_t h) {
    if (w == fbo_w && h == fbo_h && fbo[0] != 0) return; 
    fbo_w = w; fbo_h = h;
    
    destroy_fbos();

    glGenFramebuffers(2, fbo);
    glGenTextures(2, tex);
    glGenRenderbuffers(2, depth_rbo);
    glGenFramebuffers(1, &fbo_feedback);
    glGenTextures(1, &tex_feedback);

    auto setup_fbo = [&](GLuint f, GLuint t, GLuint d) {
        glBindFramebuffer(GL_FRAMEBUFFER, f);
        glBindTexture(GL_TEXTURE_2D, t);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t, 0);

        if (d > 0) {
            glBindRenderbuffer(GL_RENDERBUFFER, d);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, d);
        }
    };

    setup_fbo(fbo[0], tex[0], depth_rbo[0]);
    setup_fbo(fbo[1], tex[1], depth_rbo[1]);
    setup_fbo(fbo_feedback, tex_feedback, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, fbo_feedback);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// [UPDATED] Инициализация массива слоев и выделение FBO
void InteractiveWallpaper::apply_effect_to_output(Output* output) {
    if (!plugin_manager_) return;
    
    OutputConfig out_cfg = lua_engine.get_output_config(output->name, output->identifier);
    if (out_cfg.layers.empty()) return;

    bool egl_ready = (output->egl_surface != EGL_NO_SURFACE) && 
                     eglMakeCurrent(egl_display, output->egl_surface, output->egl_surface, egl_context);

    if (egl_ready && output->configured) {
        uint32_t buf_w = output->width * output->scale * out_cfg.fbo_scale;
        uint32_t buf_h = output->height * output->scale * out_cfg.fbo_scale;
        output->allocate_fbos(buf_w, buf_h);
    }

    std::vector<LayerInstance> new_layers;
    for (const auto& layer_cfg : out_cfg.layers) {
        bool reused = false;
        for (auto it = output->layers.begin(); it != output->layers.end(); ++it) {
            if (it->name == layer_cfg.effect_name && it->is_postprocess == layer_cfg.is_postprocess) {
                new_layers.push_back(std::move(*it));
                output->layers.erase(it);
                reused = true;
                break;
            }
        }

        if (!reused) {
            std::cout << "[Core] Loading layer '" << layer_cfg.effect_name << "' onto '" << output->name << "'" << std::endl;
            WallpaperEffectPtr eff = plugin_manager_->create_effect(layer_cfg.effect_name);
            if (eff) {
                bool init_ok = true;
                if (egl_ready && output->configured) {
                    init_ok = eff->initialize(this, output->fbo_w, output->fbo_h);
                }
                
                // ЗАЩИТА: Добавляем плагин только если он успешно инициализировался
                if (init_ok) {
                    new_layers.emplace_back(layer_cfg.effect_name, std::move(eff), layer_cfg.is_postprocess);
                } else {
                    std::cerr << "\033[31m[Core] Dropping layer '" << layer_cfg.effect_name 
                              << "' due to initialization failure.\033[0m" << std::endl;
                }
            }
        }
    }

    output->layers.clear();
    output->layers = std::move(new_layers);

    if (egl_ready && output->configured) {
        for (size_t i = 0; i < output->layers.size(); ++i) {
            auto& layer = output->layers[i];
            if (layer.effect) {
                lua_engine.apply_effect_settings(layer.effect.get(), layer.name, out_cfg.layers[i].custom_settings);
                layer.effect->resize(output->fbo_w, output->fbo_h);
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
// [UPDATED] Многослойный конвейер рендеринга (Ping-Pong FBO)
void InteractiveWallpaper::render_output(Output* output) {
    if (!output->configured || output->layers.empty() || output->egl_surface == EGL_NO_SURFACE) return;

    ZoneScoped; 
    ZoneText(output->name.c_str(), output->name.length()); 

    auto now = std::chrono::steady_clock::now();
    std::chrono::duration<float> delta_duration = now - output->last_frame_time;
    float real_dt = delta_duration.count();
    output->last_frame_time = now;

    if (real_dt > 0.1f || real_dt < 0.0f) real_dt = 0.0166f;

    output->time_since_last_render += real_dt;
    float render_dt = output->time_since_last_render;

    OutputConfig out_cfg = lua_engine.get_output_config(output->name, output->identifier);
    if (out_cfg.fps_limit > 0.0f) {
        float min_frame_time = 1.0f / out_cfg.fps_limit;
        if (output->time_since_last_render < min_frame_time) {
            // [FIX] Устранение утечки памяти Wayland
            if (output->frame_callback) {
                wl_callback_destroy(output->frame_callback);
                output->frame_callback = nullptr;
            }
            output->frame_callback = wl_surface_frame(output->surface);
            wl_callback_add_listener(output->frame_callback, &frame_listener, output);
            wl_surface_commit(output->surface); 
            return;
        }
        output->time_since_last_render = std::fmod(output->time_since_last_render, min_frame_time);
    } else {
        output->time_since_last_render = 0.0f;
    }

    if (eglMakeCurrent(egl_display, output->egl_surface, output->egl_surface, egl_context)) {
        
        {
            ZoneScopedN("Lua on_frame"); 
            lua_engine.on_frame(real_dt, output->name); 
        }

        uint32_t fbo_w = output->fbo_w;
        uint32_t fbo_h = output->fbo_h;
        glViewport(0, 0, fbo_w, fbo_h);
        
        // --- 1. Очистка стартового FBO ---
        output->current_fbo = 0;
        glBindFramebuffer(GL_FRAMEBUFFER, output->fbo[output->current_fbo]);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        {
            ZoneScopedN("Layers Pipeline"); 
            // --- 2. Оркестрация слоев ---
            for (size_t i = 0; i < output->layers.size(); ++i) {
                auto& layer = output->layers[i];
                if (!layer.effect) continue;

                if (layer.is_postprocess && i > 0) {
                    // PING-PONG SWAP
                    int next_fbo = 1 - output->current_fbo;
                    glBindFramebuffer(GL_FRAMEBUFFER, output->fbo[next_fbo]);
                    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                    
                    glActiveTexture(GL_TEXTURE0);
                    glBindTexture(GL_TEXTURE_2D, output->tex[output->current_fbo]); // u_prev_layer
                    
                    output->current_fbo = next_fbo; 
                } else {
                    glBindFramebuffer(GL_FRAMEBUFFER, output->fbo[output->current_fbo]);
                    if (i > 0) glClear(GL_DEPTH_BUFFER_BIT); // Защита 3D
                }

                // Фидбек прошлого кадра
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, output->tex_feedback); // u_feedback_layer
                glActiveTexture(GL_TEXTURE0);

                layer.effect->render(fbo_w, fbo_h, real_dt);
                
                glBindVertexArray(0);
                glUseProgram(0);
                glEnable(GL_BLEND);
                glDepthMask(GL_TRUE);
            }
        }

        // --- 3. Сохранение фидбека ---
        glBindFramebuffer(GL_READ_FRAMEBUFFER, output->fbo[output->current_fbo]);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, output->fbo_feedback);
        glBlitFramebuffer(0, 0, fbo_w, fbo_h, 0, 0, fbo_w, fbo_h, GL_COLOR_BUFFER_BIT, GL_NEAREST);

        // --- 4. Финальный вывод на экран (Упскейл) ---
        uint32_t phys_w = output->width * output->scale;
        uint32_t phys_h = output->height * output->scale;
        
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0); // EGL Backbuffer
        glBlitFramebuffer(0, 0, fbo_w, fbo_h, 0, 0, phys_w, phys_h, GL_COLOR_BUFFER_BIT, GL_LINEAR);

        // [FIX] Устранение утечки памяти Wayland
        if (output->frame_callback) {
            wl_callback_destroy(output->frame_callback);
            output->frame_callback = nullptr;
        }
        output->frame_callback = wl_surface_frame(output->surface);
        wl_callback_add_listener(output->frame_callback, &frame_listener, output);
        
        {
            ZoneScopedN("EGL Swap Buffers"); 
            eglSwapBuffers(egl_display, output->egl_surface);
        }
        
        FrameMarkNamed(output->name.c_str()); 
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

    // 1. Сохраняем логические размеры (нужны для протокола Wayland)
    output->width = width;
    output->height = height;
    output->configure_serial = serial;
    output->configured = true;

    zwlr_layer_surface_v1_ack_configure(surface, serial);

    // 2. КРИТИЧЕСКИЙ ШАГ ДЛЯ HIDPI: Говорим композитору не размывать буфер
    wl_surface_set_buffer_scale(output->surface, output->scale);

    // 3. Вычисляем физический размер для OpenGL (пиксели)
    uint32_t buffer_width = width * output->scale;
    uint32_t buffer_height = height * output->scale;

    OutputConfig out_cfg = output->parent->lua_engine.get_output_config(output->name, output->identifier);
    uint32_t target_fbo_w = buffer_width * out_cfg.fbo_scale;
    uint32_t target_fbo_h = buffer_height * out_cfg.fbo_scale;

    if (output->egl_surface == EGL_NO_SURFACE) {
        output->parent->create_egl_surface(output);

        if (output->egl_surface != EGL_NO_SURFACE) {
            if (eglMakeCurrent(output->parent->egl_display, output->egl_surface, output->egl_surface, output->parent->egl_context)) {
                output->allocate_fbos(target_fbo_w, target_fbo_h);
                for (auto& layer : output->layers) {
                    if (layer.effect) {
                        if (!layer.effect->initialize(output->parent, target_fbo_w, target_fbo_h)) {
                            layer.effect.reset();
                        }
                    }
                }
            }
        }
        if (!output->frame_callback) output->parent->render_output(output);
    } else {
        // Ресайзим буфер EGL до физических размеров
        if (output->egl_window) {
            wl_egl_window_resize(output->egl_window, buffer_width, buffer_height, 0, 0);
        }
        
        if (output->egl_surface != EGL_NO_SURFACE) {
            if (eglMakeCurrent(output->parent->egl_display, output->egl_surface, output->egl_surface, output->parent->egl_context)) {
                output->allocate_fbos(target_fbo_w, target_fbo_h);
                for (auto& layer : output->layers) {
                    if (layer.effect) layer.effect->resize(target_fbo_w, target_fbo_h);
                }
            }
        }

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

void InteractiveWallpaper::log_message(LogLevel level, const char* source, const char* message) {
    switch (level) {
        case LogLevel::INFO:    std::cout << "[INFO][" << source << "] " << message << std::endl; break;
        case LogLevel::WARNING: std::cerr << "\033[33m[WARN][" << source << "] " << message << "\033[0m" << std::endl; break;
        case LogLevel::ERR:     std::cerr << "\033[31m[ERR ][" << source << "] " << message << "\033[0m" << std::endl; break;
    }
}

