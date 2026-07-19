// Src/interactive-wallpaper.cpp
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
#include <sys/signalfd.h> // For safe signal handling via epoll
#include <signal.h>
#include <filesystem>
#include <sys/socket.h> 
#include <sys/un.h>   
#include "ipc-utils.hpp"  

// Safe Tracy inclusion: if profiling is disabled in CMake,
#ifdef TRACY_ENABLE
    #include <tracy/Tracy.hpp>
#else
    // All macros turn into no-ops with 0% CPU overhead
    #define ZoneScoped
    #define ZoneScopedN(name)
    #define ZoneText(txt, size)
    #define FrameMark
    #define FrameMarkNamed(name)
    namespace tracy { inline void SetThreadName(const char*) {} }
#endif

extern std::atomic<bool> global_running;

// --- Static Wayland protocol listeners ---
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

    // Bind the tag-based lookup function for the LuaEngine.
    // This allows the Lua Fluent API (e.g., core.get_layer("DP-1", "bg_back")) 
    // To safely resolve the C++ plugin instance dynamically per frame, 
    // Completely eliminating the risk of dangling pointers during hot-reloads.
    lua_engine.get_layer_by_tag = [this](const std::string& output_name, const std::string& tag) -> IWallpaperEffectABI* {
        for (auto& pair : outputs) {
            if (pair.second->name == output_name || output_name == "*") {
                for (auto& layer : pair.second->layers) {
                    // Check the parent layer
                    if (layer.tag == tag && layer.effect) {
                        return layer.effect.get();
                    }
                    // Check nested filters attached to this layer
                    for (auto& filter : layer.filters) {
                        if (filter.tag == tag && filter.effect) {
                            return filter.effect.get();
                        }
                    }
                }
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

// Apply settings to all layers of a specific monitor
void InteractiveWallpaper::apply_config_to_effect(Output* output) {
    if (!output || output->layers.empty()) return;
    
    OutputConfig out_cfg = lua_engine.get_output_config(output->name, output->identifier);
    output->current_fps_limit = out_cfg.fps_limit; 

    for (size_t i = 0; i < output->layers.size() && i < out_cfg.layers.size(); ++i) {
        auto& layer = output->layers[i];
        if (layer.effect) {
            // Apply settings to the main layer
            lua_engine.apply_effect_settings(layer.effect.get(), layer.name, out_cfg.layers[i].custom_settings);
            
            // Apply settings to its nested filters
            for (size_t f = 0; f < layer.filters.size() && f < out_cfg.layers[i].filters.size(); ++f) {
                auto& filter = layer.filters[f];
                if (filter.effect) {
                    lua_engine.apply_effect_settings(filter.effect.get(), filter.name, out_cfg.layers[i].filters[f].custom_settings);
                }
            }
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
    // Return a pointer to the internal string from the map (safe as long as PluginManager is alive)
    static std::string last_queried_path; 
    last_queried_path = plugin_manager_->get_bundle_path(plugin_name);
    return last_queried_path.c_str();
}

void InteractiveWallpaper::setup_inotify() {
    inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (inotify_fd < 0) return;
    
    // React only to write finalization or atomic replacement
    uint32_t mask = IN_CLOSE_WRITE | IN_MOVED_TO;

    const char* xdg_config = std::getenv("XDG_CONFIG_HOME");
    std::string config_dir = xdg_config ? std::string(xdg_config) + "/interactive-wallpaper" 
                                        : std::string(std::getenv("HOME")) + "/.config/interactive-wallpaper";

    // 1. Base user directories
    inotify_add_watch(inotify_fd, config_dir.c_str(), mask);
    inotify_add_watch(inotify_fd, (config_dir + "/plugins").c_str(), mask);

    // 2. Helper lambda for recursively adding folders
    std::error_code ec;
    auto options = std::filesystem::directory_options::follow_directory_symlink | std::filesystem::directory_options::skip_permission_denied;
    
    auto watch_dir_tree = [&](const std::string& root_path) {
        if (!std::filesystem::exists(root_path, ec)) return;

        // 1. First add the root folder itself to inotify
        inotify_add_watch(inotify_fd, root_path.c_str(), mask);

        // 2. Create explicit iterators
        auto it = std::filesystem::recursive_directory_iterator(root_path, options, ec);
        auto end = std::filesystem::recursive_directory_iterator();

        // 3. Traverse the tree
        while (it != end) {
            if (it->is_directory(ec)) {
                std::string path_str = it->path().string();

                // Check if this folder should be ignored
                if (path_str.find("/.git") != std::string::npos || 
                    path_str.find("/build") != std::string::npos ||
                    path_str.find("/node_modules") != std::string::npos) {
                    
                    // Call the ITERATOR method: it will skip entering this directory
                    it.disable_recursion_pending(); 
                } else {
                    // If the folder is useful, subscribe to it
                    inotify_add_watch(inotify_fd, path_str.c_str(), mask);
                }
            }
            
            // Safely advance to the next element
            it.increment(ec);
        }
    };

    // 3. Add all possible plugin and config locations to the watch list
    watch_dir_tree(config_dir + "/effects");           // Scenario 3 (Modder's Sandbox)
    watch_dir_tree("./src/defaults");                  // Scenario 1 (Local init.lua)
    watch_dir_tree("./plugins");                       // Scenario 1 (In-Tree shader sources)
    
    // (Optional) If CMake copies shaders directly into the build folder
    watch_dir_tree("./build-release/plugins");         
    watch_dir_tree("./build-tracy/plugins");

    watch_dir_tree(config_dir + "/scenes");

    // Add the system scenes folder (for FHS mode)
    #ifdef SYSTEM_SCENES_DIR
    watch_dir_tree(SYSTEM_SCENES_DIR);
    #endif

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
    
    // Always remove the stale socket file from a previous unclean shutdown
    unlink(socket_path.c_str()); 

    // SECURITY: Temporarily set umask to ensure the socket is created with 0600 permissions.
    // This strictly prevents other Linux users on the system from controlling your wallpaper.
    mode_t old_mask = umask(0077); 
    int bind_res = bind(ipc_fd, (struct sockaddr*)&addr, sizeof(addr));
    umask(old_mask); 

    if (bind_res < 0 || listen(ipc_fd, 5) < 0) {
        std::cerr << "[Core] Failed to bind IPC socket at " << socket_path << std::endl;
        close(ipc_fd);
        ipc_fd = -1;
        return;
    }

    // Register the main server socket in the epoll event loop
    register_epoll_fd_cxx(ipc_fd, [this](uint32_t) {
        int client_fd = accept4(ipc_fd, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (client_fd < 0) return;

        // Register the newly connected client socket for reading
        register_epoll_fd_cxx(client_fd, [this, client_fd](uint32_t events) {
            char buffer[8192]; // Optimized buffer size for handling piped Lua scripts
            
            // Zero-Latency Drain Pattern:
            // Continually read from the socket until the OS kernel signals EAGAIN/EWOULDBLOCK.
            // This prevents data buffering delays and keeps the epoll loop lean.
            while (true) {
                ssize_t bytes = read(client_fd, buffer, sizeof(buffer));

                if (bytes > 0) {
                    ipc_buffers[client_fd].append(buffer, bytes);
                    
                    // OOM (Out-Of-Memory) / Spam Protection: 
                    // Terminate connection if the accumulated payload exceeds 128 KB without a frame delimiter.
                    if (ipc_buffers[client_fd].size() > 131072) {
                        goto close_client;
                    }

                    // NULL-TERMINATED FRAMING PROTOCOL:
                    // Extract and execute all complete commands delimited by '\0'.
                    size_t pos;
                    while ((pos = ipc_buffers[client_fd].find('\0')) != std::string::npos) {
                        std::string cmd = ipc_buffers[client_fd].substr(0, pos);
                        ipc_buffers[client_fd].erase(0, pos + 1);

                        if (cmd.empty()) continue; // Safely ignore empty frames

                        // Execute the received command within the isolated Lua sandbox
                        auto result = lua_engine.get_state().safe_script(cmd, sol::script_pass_on_error);
                        
                        std::string resp;
                        if (result.valid()) {
                            resp = "OK";
                        } else {
                            sol::error err = result;
                            resp = std::string("LUA_ERR: ") + err.what();
                        }
                        
                        // CRITICAL: Append the null-byte delimiter to the outgoing response 
                        // so the CLI utility knows exactly when the server message ends.
                        resp.push_back('\0');
                        
                        // For local UNIX domain sockets, typical responses fit entirely within 
                        // the kernel's send buffer. A single non-blocking write is sufficient here.
                        write(client_fd, resp.data(), resp.size());
                    }
                } else if (bytes < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        break; // Socket drained successfully, return to epoll wait
                    }
                    if (errno == EINTR) {
                        continue; // Interrupted by a system signal, retry reading
                    }
                    goto close_client; // Unrecoverable socket error
                } else if (bytes == 0) {
                    // EOF: The client process cleanly closed the connection (or terminated)
                    goto close_client;
                }
            }
            return;

        close_client:
            ipc_buffers.erase(client_fd);
            unregister_epoll_fd(client_fd);
            close(client_fd);
        });
    });
}

// Hot-reload handler (Multi-monitor)
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
                
                // Ignore hidden files and editor temporary swap files (.swp, .tmp)
                if (name.empty() || name[0] == '.' || name.back() == '~') continue;

                if (name.find(".lua") != std::string::npos) reload_lua = true;
                else if (name.find(".glsl") != std::string::npos || 
                         name.find(".vert") != std::string::npos || 
                         name.find(".frag") != std::string::npos) reload_shader = true;
            }
        }
    }

    // --- INSTANT SAFE RELOAD ---

    if (reload_lua) {
        std::cout << "\n\033[36m[Hot-Reload] Validating Lua configuration...\033[0m" << std::endl;
        if (lua_engine.reload()) { // Includes a syntax pre-check inside!
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

                    // 1. Create a shadow (new) copy of the effect
                    WallpaperEffectPtr shadow_effect = plugin_manager_->create_effect(layer.name);
                    if (!shadow_effect) continue;

                    // 2. Attempt to initialize (shader compilation happens here)
                    if (shadow_effect->initialize(this, out->fbo_w, out->fbo_h)) {
                        // 3. Success! Perform Swap
                        layer.effect = std::move(shadow_effect);
                        std::cout << "  ✓ '" << layer.name << "' recompiled seamlessly." << std::endl;
                    } else {
                        // 4. Compilation error! 
                        // Shadow_effect will be destroyed when out of scope,
                        // And layer.effect (the old working version) will continue rendering on the screen.
                        std::cerr << "\033[31m  ✗ '" << layer.name << "' compilation failed. Keeping old version.\033[0m" << std::endl;
                    }
                }
                // Re-apply Lua settings (colors, speed) to new instances
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

    // Calculate physical dimensions in pixels
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

    // Safe termination signal interception via epoll
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
// NATIVE IN-PROCESS EGL RECOVERY (Suspend/Resume Handler)
// ==============================================================================
void InteractiveWallpaper::recover_egl_context() {
    std::cout << "\n\033[36m[Core] Initiating native in-process EGL recovery...\033[0m" << std::endl;

    // 1. Discard dead resources. 
    // DO NOT make the context current (it's physically dead).
    // DO NOT call glDelete* (it will cause NVIDIA driver Segfaults).
    for (auto& pair : outputs) {
        Output* out = pair.second.get();
        if (out->egl_surface != EGL_NO_SURFACE) {
            
            // Cleanly destroy C++ plugin instances. 
            // The OS driver has already reclaimed all GPU memory (FBOs, VAOs, VBOs).
            out->layers.clear(); 
            out->destroy_fbos(); // Only resets local handles (0)
            
            // Detach Wayland EGL
            eglDestroySurface(egl_display, out->egl_surface);
            out->egl_surface = EGL_NO_SURFACE;
            
            if (out->egl_window) {
                wl_egl_window_destroy(out->egl_window);
                out->egl_window = nullptr;
            }
        }
    }

    // 2. Terminate the dead EGL driver completely
    if (egl_context != EGL_NO_CONTEXT) {
        eglDestroyContext(egl_display, egl_context);
        egl_context = EGL_NO_CONTEXT;
    }
    if (egl_display != EGL_NO_DISPLAY) {
        eglTerminate(egl_display);
        egl_display = EGL_NO_DISPLAY;
    }

    // 3. Restart GPU connection
    if (!init_egl()) {
        std::cerr << "\033[31m[Core] CRITICAL: Failed to re-initialize EGL driver. Shutting down.\033[0m" << std::endl;
        global_running = false;
        this->stop();
        return;
    }

    // 4. Rebuild the visual pipeline entirely from scratch
    for (auto& pair : outputs) {
        Output* out = pair.second.get();
        if (out->width > 0 && out->height > 0) {
            create_egl_surface(out);
            
            // By calling apply_effect_to_output, we force the PluginManager 
            // to instantiate fresh copies of the plugins and reload their state from Lua.
            // This bypasses the need for an ABI change entirely!
            apply_effect_to_output(out);
            if (!out->frame_callback) {
                this->render_output(out);
            }
        }
    }
    std::cout << "\033[32m[Core] GPU Recovery successful! Resuming render loop.\033[0m\n" << std::endl;
}

// ==============================================================================
// MAIN EVENT LOOP (ZERO-LATENCY EPOLL BASED)
// ==============================================================================

void InteractiveWallpaper::run() {
    tracy::SetThreadName("Wayland Event Loop"); // Thread name in profiler
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
                // SECURITY: Catch broken pipes or closed sockets before executing the callback.
                // Prevents 100% CPU usage in an infinite epoll_wait loop.
                if (revents & (EPOLLERR | EPOLLHUP)) {
                    std::cerr << "[Core] Epoll detected broken FD: " << fd << ". Unregistering safely." << std::endl;
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
                    epoll_callbacks.erase(fd);
                    close(fd);
                    continue; // Skip the callback execution
                }
                
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

// FBO memory management
void InteractiveWallpaper::Output::destroy_fbos() {
    if (fbo[0]) { glDeleteFramebuffers(2, fbo); fbo[0] = fbo[1] = 0; }
    if (tex[0]) { glDeleteTextures(2, tex); tex[0] = tex[1] = 0; }
    if (depth_rbo[0]) { glDeleteRenderbuffers(2, depth_rbo); depth_rbo[0] = depth_rbo[1] = 0; }
    if (tex_feedback) { glDeleteTextures(1, &tex_feedback); tex_feedback = 0; }
}

void InteractiveWallpaper::Output::allocate_fbos(uint32_t w, uint32_t h) {
    if (w == fbo_w && h == fbo_h && fbo[0] != 0) return; 
    fbo_w = w; fbo_h = h;
    
    destroy_fbos();

    glGenFramebuffers(2, fbo);
    glGenTextures(2, tex);
    glGenRenderbuffers(2, depth_rbo);
    
    // Allocate only the texture for feedback history, no FBO needed
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

    // Initialize feedback texture with black pixels
    glBindTexture(GL_TEXTURE_2D, tex_feedback);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}


// Initialize the array of visual layers and allocate required Framebuffer Objects (FBOs)
void InteractiveWallpaper::apply_effect_to_output(Output* output) {
    if (!plugin_manager_) return;
    

    // 1. Fetch the multi-layer configuration for this specific physical monitor
    OutputConfig out_cfg = lua_engine.get_output_config(output->name, output->identifier);
    output->current_fps_limit = out_cfg.fps_limit; 

    if (out_cfg.layers.empty()) return;

    // Check if the EGL context is ready for this output
    bool egl_ready = (output->egl_surface != EGL_NO_SURFACE) && 
                     eglMakeCurrent(egl_display, output->egl_surface, output->egl_surface, egl_context);

    // 2. Allocate Ping-Pong Framebuffers based on monitor resolution and user scaling factor
    if (egl_ready && output->configured) {
        uint32_t buf_w = output->width * output->scale * out_cfg.fbo_scale;
        uint32_t buf_h = output->height * output->scale * out_cfg.fbo_scale;
        output->allocate_fbos(buf_w, buf_h);
    }

    // ==============================================================================
    // 3. HOT-RELOAD & REUSE PIPELINE (Zero-Allocation Algorithm)
    // ==============================================================================
    std::vector<LayerInstance> new_layers;
    new_layers.reserve(out_cfg.layers.size());

    for (const auto& layer_cfg : out_cfg.layers) {
        LayerInstance current_layer("", "", nullptr, false);
        bool reused = false;
        
        auto it = std::find_if(output->layers.begin(), output->layers.end(), [&](const LayerInstance& l) {
            return l.effect != nullptr && l.name == layer_cfg.effect_name && 
                   l.tag == layer_cfg.tag && l.is_postprocess == layer_cfg.is_postprocess;
        });

        if (it != output->layers.end()) {
            current_layer = std::move(*it);
            reused = true;
        } else {
            // 4. Instantiation (If no reusable parent layer was found)
            std::cout << "[Core] Loading layer '" << layer_cfg.effect_name 
                      << "' (Tag: '" << layer_cfg.tag << "') onto '" << output->name << "'" << std::endl;
                      
            WallpaperEffectPtr eff = plugin_manager_->create_effect(layer_cfg.effect_name);
            if (eff) {
                if (egl_ready && output->configured) eff->initialize(this, output->fbo_w, output->fbo_h);
                current_layer = LayerInstance(layer_cfg.effect_name, layer_cfg.tag, std::move(eff), layer_cfg.is_postprocess, layer_cfg.clear_depth);
            } else {
                std::cerr << "\033[31m[Core] Dropping layer '" << layer_cfg.effect_name << "' due to initialization failure.\033[0m" << std::endl;
            }
        }

        // --- NEW: Filter Instantiation & Reuse Loop ---
        if (current_layer.effect) {
            std::vector<LayerInstance> new_filters;
            new_filters.reserve(layer_cfg.filters.size());

            for (const auto& filter_cfg : layer_cfg.filters) {
                bool f_reused = false;
                
                // Search in the old filters of the current layer
                auto f_it = std::find_if(current_layer.filters.begin(), current_layer.filters.end(), [&](const LayerInstance& f) {
                    return f.effect != nullptr && f.name == filter_cfg.effect_name && f.tag == filter_cfg.tag;
                });

                if (f_it != current_layer.filters.end()) {
                    new_filters.push_back(std::move(*f_it));
                    f_reused = true;
                } else {
                    std::cout << "  -> Stacking filter '" << filter_cfg.effect_name 
                              << "' (Tag: '" << filter_cfg.tag << "')" << std::endl;
                    
                    WallpaperEffectPtr f_eff = plugin_manager_->create_effect(filter_cfg.effect_name);
                    if (f_eff) {
                        bool init_ok = true;
                        if (egl_ready && output->configured) {
                            // CHECK INITIALIZATION RESULT!
                            init_ok = f_eff->initialize(this, output->fbo_w, output->fbo_h);
                        }
                        
                        if (init_ok) {
                            new_filters.emplace_back(filter_cfg.effect_name, filter_cfg.tag, std::move(f_eff), true, false);
                        } else {
                            std::cerr << "\033[31m[Core] Dropping filter '" << filter_cfg.effect_name 
                                      << "' due to initialization failure.\033[0m" << std::endl;
                        }
                    }
                }
            }
            // Swap the old dead filters out and insert the new valid ones
            current_layer.filters = std::move(new_filters);
            new_layers.push_back(std::move(current_layer));
        }
    }

    // 5. Cleanup the dead layers.
    output->layers.clear(); 
    output->layers = std::move(new_layers);

    // ==============================================================================
    // 6. CASCADING CONFIGURATION APPLICATION
    // ==============================================================================
    if (egl_ready && output->configured) {
        for (size_t i = 0; i < output->layers.size(); ++i) {
            auto& layer = output->layers[i];
            if (layer.effect) {
                lua_engine.apply_effect_settings(layer.effect.get(), layer.name, out_cfg.layers[i].custom_settings);
                layer.effect->resize(output->fbo_w, output->fbo_h);
                
                // Trigger resize and settings for the filters too
                for (size_t j = 0; j < layer.filters.size(); ++j) {
                    auto& filter = layer.filters[j];
                    if (filter.effect) {
                        lua_engine.apply_effect_settings(filter.effect.get(), filter.name, out_cfg.layers[i].filters[j].custom_settings);
                        filter.effect->resize(output->fbo_w, output->fbo_h);
                    }
                }
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


void InteractiveWallpaper::render_output(Output* output) {
    // ==============================================================================
    // 1. SAFETY CHECKS
    // ==============================================================================
    // Ensure the physical monitor is fully initialized by the Wayland compositor,
    // has active visual layers assigned by Lua, and possesses a valid EGL surface.
    if (!output->configured || output->layers.empty() || output->egl_surface == EGL_NO_SURFACE) return;

    ZoneScoped; 
    ZoneText(output->name.c_str(), output->name.length()); 

    // ==============================================================================
    // 2. DELTA TIME & HARDWARE FPS LIMITER
    // ==============================================================================
    // Calculate precise elapsed time since the LAST RENDERED frame.
    auto now = std::chrono::steady_clock::now();
    std::chrono::duration<float> delta_duration = now - output->last_frame_time;
    float dt_since_last_render = delta_duration.count();

    // Zero-CPU-Overhead FPS limiter via Linux timerfd
    if (output->current_fps_limit > 0.0f) {
        float min_frame_time = 1.0f / output->current_fps_limit;
        
        if (dt_since_last_render < min_frame_time) {
            // Frame is too early. Calculate remaining time until the next frame is allowed.
            float delay_s = min_frame_time - dt_since_last_render;
            
            // Arm the hardware timerfd. The main Wayland epoll loop will sleep 
            // at 0% CPU usage and wake up exactly when this timer expires.
            if (output->fps_timer_fd >= 0) {
                struct itimerspec ts{};
                ts.it_value.tv_sec = static_cast<time_t>(delay_s);
                ts.it_value.tv_nsec = static_cast<long>((delay_s - ts.it_value.tv_sec) * 1e9);
                timerfd_settime(output->fps_timer_fd, 0, &ts, nullptr);
            }
            // EARLY EXIT: Do NOT update last_frame_time here!
            // Updating it now would cause "Delta Time Starvation", resulting in 
            // stuttering physics and broken mathematical animations in Lua.
            return; 
        }
    }

    // FPS check passed. Commit to rendering this frame.
    output->last_frame_time = now;
    float frame_dt = dt_since_last_render;

    // Clamp delta time to prevent physics explosions (e.g., objects flying to infinity)
    // after the system wakes up from a long Suspend/Sleep cycle.
    if (frame_dt > 0.1f || frame_dt < 0.0f) {
        frame_dt = 0.0166f; // Fallback to a stable 60Hz delta
    }

    // ==============================================================================
    // 3. EGL CONTEXT ACTIVATION
    // ==============================================================================
    if (eglMakeCurrent(egl_display, output->egl_surface, output->egl_surface, egl_context)) {
        
        {
            ZoneScopedN("Lua on_frame"); 
            // Execute the Lua Control-Plane hook for smooth mathematical animation.
            // Lua handles camera positioning and logic, pushing results into the BlackBoard.
            lua_engine.on_frame(frame_dt, output->name); 
        }

        uint32_t fbo_w = output->fbo_w;
        uint32_t fbo_h = output->fbo_h;
        glViewport(0, 0, fbo_w, fbo_h);
        
        // --- Prepare the starting FBO (Framebuffer Object) ---
        output->current_fbo = 0;
        glBindFramebuffer(GL_FRAMEBUFFER, output->fbo[output->current_fbo]);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Standard blending for typical 2D/3D compositing
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        {
            ZoneScopedN("Layers Pipeline"); 
            // ========================================================================
            // 4. MULTI-LAYER RENDERING PIPELINE
            // ========================================================================
            for (size_t i = 0; i < output->layers.size(); ++i) {
                auto& layer = output->layers[i];
                if (!layer.effect) continue;

                if (layer.is_postprocess && i > 0) {
                    // --- GLOBAL PING-PONG FBO SWAP (For Screen-Space Post-Processing) ---
                    int next_fbo = 1 - output->current_fbo;
                    glBindFramebuffer(GL_FRAMEBUFFER, output->fbo[next_fbo]);
                    
                    // POST-PROCESS OPTIMIZATION 1: ROP Bypass
                    // Post-processing overwrites the entire screen. Disabling blending 
                    // prevents the GPU from reading the destination pixel, saving huge bandwidth.
                    glDisable(GL_BLEND);
                    glDisable(GL_DEPTH_TEST);
                    
                    glActiveTexture(GL_TEXTURE0);
                    glBindTexture(GL_TEXTURE_2D, output->tex[output->current_fbo]); // Bind U_prev_layer
                    
                    output->current_fbo = next_fbo; 
                } else {
                    // --- STANDARD RENDERING ---
                    glBindFramebuffer(GL_FRAMEBUFFER, output->fbo[output->current_fbo]);
                    
                    // Restore standard 3D states
                    glEnable(GL_BLEND);
                    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                    glEnable(GL_DEPTH_TEST);
                    
                    if (i > 0 && layer.clear_depth) {
                        glClear(GL_DEPTH_BUFFER_BIT); 
                    }
                }

                // Provide previous frame's feedback texture (GL_TEXTURE1)
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, output->tex_feedback); // Bind U_feedback_layer
                glActiveTexture(GL_TEXTURE0); // Reset active texture unit

                // ====================================================================
                // --- INJECT NESTED FILTERS (Node-Based Rendering) ---
                // We pass the instantiated filter plugins into the base effect via C-ABI.
                // The base effect plugin will handle its own internal Ping-Pong FBO 
                // isolation, executing these filters seamlessly if the array is not empty.
                // ====================================================================
                if (!layer.filters.empty()) {
                    std::vector<IWallpaperEffectABI*> raw_filters;
                    raw_filters.reserve(layer.filters.size());
                    for (auto& f : layer.filters) {
                        if (f.effect) raw_filters.push_back(f.effect.get());
                    }
                    layer.effect->set_filters_abi(raw_filters.data(), raw_filters.size());
                } else {
                    layer.effect->set_filters_abi(nullptr, 0); // Clear active filters
                }

                // Execute the main C++ plugin logic (Draw Calls)
                layer.effect->render(fbo_w, fbo_h, frame_dt);
                
                // --- DEFENSIVE STATE ISOLATION ---
                // Crucial in modular engines: never trust the plugin to reset its state.
                glBindVertexArray(0);
                glUseProgram(0);
                glDepthMask(GL_TRUE); 
            }
        }

        // ========================================================================
        // 5. FEEDBACK LOOP PRESERVATION (Zero-Copy Texture Swap)
        // ========================================================================
        // POST-PROCESS OPTIMIZATION 2: Memory Bandwidth Saver
        // Instead of using glBlitFramebuffer (which physically copies millions of pixels),
        // we perform an O(1) Handle Swap. We detach the current rendered texture from 
        // the FBO and swap its ID with the feedback texture.
        bool needs_feedback = false;
        for (const auto& layer : output->layers) {
            if (layer.is_postprocess) {
                needs_feedback = true;
                break;
            }
        }

        if (needs_feedback) {
            GLuint current_tex = output->tex[output->current_fbo];
            
            // 1. Swap the OpenGL texture IDs
            output->tex[output->current_fbo] = output->tex_feedback;
            output->tex_feedback = current_tex;
            
            // 2. Re-attach the new (now recycled) texture to the current FBO
            // The FBO remains perfectly valid, but now points to the recycled memory.
            glBindFramebuffer(GL_FRAMEBUFFER, output->fbo[output->current_fbo]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, output->tex[output->current_fbo], 0);
        }

        // ========================================================================
        // 6. FINAL SCREEN OUTPUT (Upscaling)
        // ========================================================================
        uint32_t phys_w = output->width * output->scale;
        uint32_t phys_h = output->height * output->scale;
        
        // Target the actual Wayland EGL Backbuffer
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0); 
        // Blit (copy and scale) the internal FBO to the physical monitor resolution
        glBlitFramebuffer(0, 0, fbo_w, fbo_h, 0, 0, phys_w, phys_h, GL_COLOR_BUFFER_BIT, GL_LINEAR);

        // ========================================================================
        // [WAYLAND OPTIMIZATION]: Ghosting Prevention
        // Some plugins might leave Alpha < 1.0 in the FBO. Since the wallpaper is 
        // the absolute bottom layer, we force the final EGL Backbuffer to be 100% 
        // opaque. This prevents Wayland from performing expensive hardware blending 
        // against the empty void behind the wallpaper, eliminating visual ghosting.
        // ========================================================================
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE); // Target only the Alpha channel
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);               // Set Alpha to 1.0 (Opaque)
        glClear(GL_COLOR_BUFFER_BIT);                       // glClear bypasses glViewport automatically
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);    // Restore standard color write mask

        // ========================================================================
        // 7. WAYLAND FRAME SYNCHRONIZATION
        // ========================================================================
        // Request the compositor to notify us exactly when this frame hits the display.
        // This guarantees V-Sync and prevents tearing.
        if (output->frame_callback) {
            wl_callback_destroy(output->frame_callback);
            output->frame_callback = nullptr;
        }
        output->frame_callback = wl_surface_frame(output->surface);
        wl_callback_add_listener(output->frame_callback, &frame_listener, output);
        
        {
            ZoneScopedN("EGL Swap Buffers"); 
            EGLBoolean swap_result = eglSwapBuffers(egl_display, output->egl_surface);
            
            // ====================================================================
            // 8. NATIVE EGL RECOVERY LOGIC (Suspend/Resume Handler)
            // ====================================================================
            // Handles GPU suspend/resume cycles seamlessly without requiring a daemon restart.
            if (swap_result == EGL_FALSE) {
                EGLint error = eglGetError();
                if (error == EGL_CONTEXT_LOST) {
                    std::cerr << "\n\033[33m[EGL] WARNING: EGL_CONTEXT_LOST detected. GPU woke from sleep?\033[0m" << std::endl;
                    
                    // Trigger the native in-process recovery state machine.
                    // This will rebuild the EGL driver, FBOs, and reload C++ plugins.
                    this->recover_egl_context();
                    
                    // Abort rendering this frame. The next Wayland tick will use the revived context.
                    return; 
                    
                } else {
                    std::cerr << "\033[31m[EGL] Error: eglSwapBuffers failed: 0x" 
                              << std::hex << error << std::dec << "\033[0m" << std::endl;
                }
            }
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

    // 1. Save logical dimensions (needed for the Wayland protocol)
    output->width = width;
    output->height = height;
    output->configure_serial = serial;
    output->configured = true;

    zwlr_layer_surface_v1_ack_configure(surface, serial);

    // 2. CRITICAL STEP FOR HIDPI: Tell the compositor not to blur the buffer
    wl_surface_set_buffer_scale(output->surface, output->scale);

    // ========================================================================
    // [WAYLAND OPTIMIZATION]: Disable Compositor Alpha Blending
    // Inform the compositor that our wallpaper is 100% opaque.
    // This dramatically reduces GPU memory bandwidth usage on the desktop.
    // ========================================================================
    struct wl_region* opaque_region = wl_compositor_create_region(output->parent->compositor);
    if (opaque_region) {
        // Note: Wayland regions use logical (scaled) coordinates, not physical pixels
        wl_region_add(opaque_region, 0, 0, width, height);
        wl_surface_set_opaque_region(output->surface, opaque_region);
        wl_region_destroy(opaque_region); // Safe to destroy immediately
    }

    // 3. Calculate physical dimensions for OpenGL (pixels)
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
                        // Initial C++ plugin initialization (shader compilation)
                        if (!layer.effect->initialize(output->parent, target_fbo_w, target_fbo_h)) {
                            layer.effect.reset();
                        }
                        
                        // --- ИСПРАВЛЕНИЕ: ИНИЦИАЛИЗАЦИЯ ВЛОЖЕННЫХ ФИЛЬТРОВ ---
                        for (auto& filter : layer.filters) {
                            if (filter.effect) {
                                if (!filter.effect->initialize(output->parent, target_fbo_w, target_fbo_h)) {
                                    filter.effect.reset(); // Отбрасываем фильтр, если он сломался
                                }
                            }
                        }
                    }
                }
                // Apply Lua settings immediately after EGL context initialization
                output->parent->apply_config_to_effect(output);
            }
            
            // BUGFIX: Register the FPS limiter timerfd in the epoll loop EXACTLY ONCE upon creation!
            if (output->fps_timer_fd >= 0 && output->parent) {
                output->parent->register_epoll_fd_cxx(output->fps_timer_fd, [output](uint32_t) {
                    uint64_t expirations;
                    read(output->fps_timer_fd, &expirations, sizeof(expirations));
                    // The timer fired, now we are allowed to render and commit to Wayland
                    if (output->parent) output->parent->render_output(output);
                });
            }
        }
        if (!output->frame_callback) output->parent->render_output(output);
    } else {
        // Resize the EGL buffer to physical dimensions
        if (output->egl_window) {
            wl_egl_window_resize(output->egl_window, buffer_width, buffer_height, 0, 0);
        }
        
        if (output->egl_surface != EGL_NO_SURFACE) {
            if (eglMakeCurrent(output->parent->egl_display, output->egl_surface, output->egl_surface, output->parent->egl_context)) {
                output->allocate_fbos(target_fbo_w, target_fbo_h);
                for (auto& layer : output->layers) {
                    if (layer.effect) {
                        layer.effect->resize(target_fbo_w, target_fbo_h);
                        
                        // --- ИСПРАВЛЕНИЕ: РЕСАЙЗ ВЛОЖЕННЫХ ФИЛЬТРОВ ---
                        for (auto& filter : layer.filters) {
                            if (filter.effect) filter.effect->resize(target_fbo_w, target_fbo_h);
                        }
                    }
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

