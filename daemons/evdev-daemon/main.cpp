// Main.cpp
#include <libevdev/libevdev.h>
#include <linux/input.h>

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <poll.h>
#include <unistd.h>
#include <algorithm>
#include <cmath>

#include <csignal>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include <chrono>

#include <shader-desk/ipc-utils.hpp>
#include "pointer-data.hpp"

static volatile sig_atomic_t running = 1;
static void handle_sig(int) { running = 0; }

struct Device {
    int fd = -1;
    libevdev* dev = nullptr;
    std::string path;
    std::string name;
    bool has_rel = false;
    bool has_abs = false;
    
    float abs_x_min = 0.0f, abs_x_max = 0.0f, abs_x_range = 1.0f;
    float abs_y_min = 0.0f, abs_y_max = 0.0f, abs_y_range = 1.0f;
    
    // For ABS devices - previous positions
    float prev_abs_x = 0.0f;
    float prev_abs_y = 0.0f;
    bool has_prev_abs = false; 
    
    // --- Normalized absolute coordinates [0.0, 1.0] ---
    float abs_x_norm = 0.0f;
    float abs_y_norm = 0.0f;
    
    // To accumulate changes within a single event packet
    float pending_dx = 0.0f;
    float pending_dy = 0.0f;
    bool has_pending_event = false;

    bool is_touch_event = false;
};

static int create_send_socket() {
    // SOCK_NONBLOCK ensures we never hang during transmission
    int s = socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    if (s < 0) {
        std::cerr << "socket() failed: " << strerror(errno) << "\n";
    }
    return s;
}

static std::vector<std::string> list_event_devices() {
    std::vector<std::string> out;
    const char* devdir = "/dev/input";
    DIR* d = opendir(devdir);
    if (!d) return out;
    struct dirent* ent;
    while ((ent = readdir(d)) != nullptr) {
        if (strncmp(ent->d_name, "event", 5) == 0) {
            out.emplace_back(std::string(devdir) + "/" + ent->d_name);
        }
    }
    closedir(d);
    return out;
}

static bool already_added(const std::vector<Device>& devs, const std::string& path) {
    for (auto const& d : devs) if (d.path == path) return true;
    return false;
}

static void scan_and_add_devices(std::vector<Device>& devices) {
    auto list = list_event_devices();
    for (auto& p : list) {

        if (already_added(devices, p)) continue;
        int fd = open(p.c_str(), O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        libevdev* dev = nullptr;
        int rc = libevdev_new_from_fd(fd, &dev);
        if (rc < 0) { close(fd); continue; }

        bool is_keyboard = libevdev_has_event_code(dev, EV_KEY, KEY_A) || 
                   libevdev_has_event_code(dev, EV_KEY, KEY_ENTER);

        if (is_keyboard) {
            libevdev_free(dev);
            close(fd);
            continue; // Skip keyboards for security reasons!
        }

        bool has_rel = libevdev_has_event_code(dev, EV_REL, REL_X) && libevdev_has_event_code(dev, EV_REL, REL_Y);
        bool has_abs = libevdev_has_event_code(dev, EV_ABS, ABS_X) && libevdev_has_event_code(dev, EV_ABS, ABS_Y);

        if (!has_rel && !has_abs) {
            libevdev_free(dev);
            close(fd);
            continue;
        }

        Device D;
        D.fd = fd;
        D.dev = dev;
        D.path = p;
        D.name = libevdev_get_name(dev);
        D.has_rel = has_rel;
        D.has_abs = has_abs;

        if (has_abs) {
            const struct input_absinfo* ax = libevdev_get_abs_info(dev, ABS_X);
            const struct input_absinfo* ay = libevdev_get_abs_info(dev, ABS_Y);
            
            if (ax) {
                D.abs_x_min = static_cast<float>(ax->minimum);
                D.abs_x_max = static_cast<float>(ax->maximum);
                D.abs_x_range = D.abs_x_max - D.abs_x_min;
                D.prev_abs_x = static_cast<float>(ax->value);
            }
            if (ay) {
                D.abs_y_min = static_cast<float>(ay->minimum);
                D.abs_y_max = static_cast<float>(ay->maximum);
                D.abs_y_range = D.abs_y_max - D.abs_y_min;
                D.prev_abs_y = static_cast<float>(ay->value);
            }
            D.has_prev_abs = true;
        }

        devices.push_back(std::move(D));
        std::cerr << "Added device: " << p << " name: " << D.name 
                  << " (rel=" << has_rel << " abs=" << has_abs << ")\n";
    }
}

static void send_pending_event(Device* dptr, int send_sock) {
    if (dptr->has_pending_event) {
        
        // IMPORTANT: Zero-initialization {} clears struct padding to prevent memory leaks
        PointerDatagram datagram{}; 
    
        datagram.rel_dx = dptr->pending_dx; 
        datagram.rel_dy = dptr->pending_dy;
        datagram.abs_x  = dptr->abs_x_norm;
        datagram.abs_y  = dptr->abs_y_norm;
        datagram.is_absolute = dptr->has_abs ? 1 : 0;

        // Configure IPC UNIX socket address
        struct sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        
        std::string socket_path = shader_desk::get_ipc_socket_path("shader-desk-pointer");
        strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);
        socklen_t addr_len = sizeof(sa_family_t) + socket_path.length() + 1;

        // Fire-and-Forget non-blocking send
        ssize_t rc = sendto(send_sock, &datagram, sizeof(datagram), MSG_DONTWAIT, 
                           (struct sockaddr*)&addr, addr_len);
        
        if (rc < 0) {
            // Fault Tolerance: Gracefully ignore expected IPC states.
            // ECONNREFUSED / ENOENT: Wayland Core is offline or reloading
            // EAGAIN / EWOULDBLOCK: Core buffer is full, drop the packet (Zero-Latency)
            // EINTR: Interrupted by system signal
            if (errno != ECONNREFUSED && errno != ENOENT && 
                errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                
                // Rate-limit critical error logging (max 1 per 5 seconds) to prevent terminal spam
                static auto last_err_time = std::chrono::steady_clock::time_point();
                auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration_cast<std::chrono::seconds>(now - last_err_time).count() > 5) {
                    std::cerr << "[Evdev Daemon] IPC socket error: " << strerror(errno) << "\n";
                    last_err_time = now;
                }
            }
        }
    }
    
    // Reset accumulated changes for the next batch
    dptr->pending_dx = 0.0f;
    dptr->pending_dy = 0.0f;
    dptr->has_pending_event = false;
}

int main(int argc, char** argv) {
    // Command line arguments are no longer needed
    (void)argc; (void)argv;

    signal(SIGINT, handle_sig);
    signal(SIGTERM, handle_sig);

    std::vector<Device> devices;
    scan_and_add_devices(devices);

    int send_sock = create_send_socket();
    if (send_sock < 0) return 1;

    std::cout << "evdev-pointer-daemon starting. Routing events to '@shader-desk-pointer'...\n";

    const int RESCAN_INTERVAL_MS = 5000;
    auto last_scan = std::chrono::steady_clock::now();

    while (running) {
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_scan).count() > RESCAN_INTERVAL_MS) {
            scan_and_add_devices(devices);
            last_scan = now;
        }

        std::vector<struct pollfd> pfds;
        pfds.reserve(devices.size());
        for (auto &d : devices) {
            if (d.fd >= 0) {
                struct pollfd pfd;
                pfd.fd = d.fd;
                pfd.events = POLLIN;
                pfd.revents = 0;
                pfds.push_back(pfd);
            }
        }

        int timeout = 500;
        int rc = poll(pfds.data(), pfds.size(), timeout);
        if (rc < 0) {
            if (errno == EINTR) continue;
            std::cerr << "poll() failed: " << strerror(errno) << "\n";
            break;
        }

        for (auto &pfd : pfds) {
            if (!(pfd.revents & POLLIN)) continue;
            
            Device* dptr = nullptr;
            for (auto &d : devices) if (d.fd == pfd.fd) { dptr = &d; break; }
            if (!dptr) continue;

            struct input_event ev;
            while (true) {
                int r = libevdev_next_event(dptr->dev, LIBEVDEV_READ_FLAG_NORMAL, &ev);
                if (r == LIBEVDEV_READ_STATUS_SUCCESS) {
                    
                    // SYN_REPORT (EV_SYN) events signify the end of an event packet
                    if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
                        send_pending_event(dptr, send_sock);
                        continue;
                    }

                    if (ev.type == EV_REL) {
                        if (ev.code == REL_X) {
                            dptr->pending_dx += static_cast<float>(ev.value);
                            dptr->has_pending_event = true;
                        } else if (ev.code == REL_Y) {
                            dptr->pending_dy += static_cast<float>(ev.value);
                            dptr->has_pending_event = true;
                        }
                    } else if (ev.type == EV_ABS) {
                        if (ev.code == ABS_X || ev.code == ABS_Y) {
                            
                            // Mark as a touch if there were no previous coordinates
                            dptr->is_touch_event = !dptr->has_prev_abs;
                            
                            if (ev.code == ABS_X) {
                                if (dptr->has_prev_abs) {
                                    float diff = static_cast<float>(ev.value) - dptr->prev_abs_x;
                                    if (dptr->is_touch_event) diff *= 0.3f;
                                    dptr->pending_dx += diff / dptr->abs_x_range;
                                }
                                dptr->prev_abs_x = static_cast<float>(ev.value);
                                
                                // Calculate strict [0.0, 1.0] normalized coordinate for screen mapping
                                dptr->abs_x_norm = (static_cast<float>(ev.value) - dptr->abs_x_min) / dptr->abs_x_range;
                                
                                dptr->has_prev_abs = true;
                            } else if (ev.code == ABS_Y) {
                                if (dptr->has_prev_abs) {
                                    float diff = static_cast<float>(ev.value) - dptr->prev_abs_y;
                                    if (dptr->is_touch_event) diff *= 0.3f;
                                    dptr->pending_dy += diff / dptr->abs_y_range;
                                }
                                dptr->prev_abs_y = static_cast<float>(ev.value);
                                
                                // Calculate strict [0.0, 1.0] normalized coordinate for screen mapping
                                dptr->abs_y_norm = (static_cast<float>(ev.value) - dptr->abs_y_min) / dptr->abs_y_range;
                                
                                dptr->has_prev_abs = true;
                            }
                            dptr->has_pending_event = true;
                        }
                    }
                } else if (r == LIBEVDEV_READ_STATUS_SYNC) {
                    send_pending_event(dptr, send_sock);
                    continue;
                } else if (r == -EAGAIN) {
                    if (dptr->has_pending_event) {
                        send_pending_event(dptr, send_sock);
                    }
                    break;
                } else {
                    if (dptr->has_pending_event) {
                        send_pending_event(dptr, send_sock);
                    }
                    std::cerr << "Device disconnected: " << dptr->path << "\n";
                    if (dptr->dev) { libevdev_free(dptr->dev); dptr->dev = nullptr; }
                    if (dptr->fd >= 0) { close(dptr->fd); dptr->fd = -1; }
                    break;
                }
            }
        }

        // Remove disconnected devices
        devices.erase(std::remove_if(devices.begin(), devices.end(),
                                     [](const Device& d){ return d.fd == -1 && d.dev == nullptr; }),
                      devices.end());
    }

    for (auto &d : devices) {
        if (d.dev) libevdev_free(d.dev);
        if (d.fd >= 0) close(d.fd);
    }
    if (send_sock >= 0) close(send_sock);
    
    std::cout << "evdev-pointer-daemon exiting cleanly.\n";
    return 0;
}