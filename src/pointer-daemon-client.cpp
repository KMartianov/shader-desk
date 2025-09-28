#include "pointer-daemon-client.hpp"
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

PointerDaemonClient::PointerDaemonClient() {}

PointerDaemonClient::~PointerDaemonClient() {
    disconnect();
}
// Helper function (should be in a common header)
std::string get_default_socket_path() {
    const char* xr = std::getenv("XDG_RUNTIME_DIR");
    if (xr && xr[0]) return std::string(xr) + "/evdev-pointer.sock";
    uid_t uid = getuid();
    return std::string("/tmp/evdev-pointer-") + std::to_string(uid) + ".sock";
}

bool PointerDaemonClient::connect(const std::string& path) {
    if (connected) disconnect();

    socket_path = path.empty() ? get_default_socket_path() : path;
    
    sockfd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        std::cerr << "Failed to create socket: " << strerror(errno) << std::endl;
        return false;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    
    // Убедимся, что путь сокета не слишком длинный
    if (socket_path.size() >= sizeof(addr.sun_path)) {
        std::cerr << "Socket path is too long: " << socket_path << std::endl;
        close(sockfd);
        sockfd = -1;
        return false;
    }
    
    // Копируем путь и удаляем старый файл сокета, если он существует
    strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);
    unlink(socket_path.c_str()); 
    
    // Привязываем сокет к пути, чтобы он мог получать сообщения
    if (bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "Failed to bind socket to path " << socket_path << ": " << strerror(errno) << std::endl;
        close(sockfd);
        sockfd = -1;
        return false;
    }
    
    connected = true;
    running = true;
    receiver_thread = std::thread(&PointerDaemonClient::receive_loop, this);
    
    std::cout << "Pointer daemon client is listening on: " << socket_path << std::endl;
    return true;
}

void PointerDaemonClient::disconnect() {
    running = false;
    connected = false;
    
    if (sockfd >= 0) {
        close(sockfd);
        sockfd = -1;
    }
    
    if (receiver_thread.joinable()) {
        receiver_thread.join();
    }
}

void PointerDaemonClient::set_callbacks(MotionCallback motion_cb, MoveCallback move_cb, ClickCallback click_cb) {
    on_motion = motion_cb;
    on_move = move_cb;
    on_click = click_cb;
}

void PointerDaemonClient::receive_loop() {
    char buffer[4096];
    struct pollfd pfd;
    pfd.fd = sockfd;
    pfd.events = POLLIN;

    while (running) {
        int ret = poll(&pfd, 1, 100); // 100ms timeout
        if (ret < 0) {
            if (errno == EINTR) continue;
            std::cerr << "Poll error: " << strerror(errno) << std::endl;
            break;
        }
        
        if (ret == 0) continue; // Timeout
        
        if (pfd.revents & POLLIN) {
            ssize_t n = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
            if (n > 0) {
                buffer[n] = '\0';
                parse_message(std::string(buffer, n));
            } else if (n == 0) {
                std::cout << "Pointer daemon disconnected" << std::endl;
                break;
            } else {
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    std::cerr << "Receive error: " << strerror(errno) << std::endl;
                    break;
                }
            }
        }
    }
    
    connected = false;
}

void PointerDaemonClient::parse_message(const std::string& json_msg) {
    try {
        auto msg = json::parse(json_msg);
        
        std::string type = msg.value("type", "unknown");
        double dx = msg.value("dx", 0.0);
        double dy = msg.value("dy", 0.0);
        bool normalized = msg.value("normalized", false); 
        
        if (type == "rel" || type == "abs") {
            double vx = msg.value("vx", 0.0);
            double vy = msg.value("vy", 0.0);
            double dt = msg.value("dt", 0.0);
            std::string device_name = msg.value("name", ""); 

            // Если демон прислал нули, но есть смещение,
            // попробуем оценить скорость сами.
            if (std::abs(vx) < 1e-6 && std::abs(dx) > 1e-9) {
                dt = 1.0 / 60.0; // Предполагаем 60 FPS
                vx = dx / dt;
                vy = dy / dt;
            }
            
            if (on_motion) {
                on_motion(dx, dy, vx, vy, dt, normalized, device_name); 
            }
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Failed to parse pointer message: " << e.what() << std::endl;
        std::cerr << "Message: " << json_msg << std::endl;
    }
}

