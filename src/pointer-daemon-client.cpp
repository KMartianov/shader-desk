#include "pointer-daemon-client.hpp"
#include "ipc-protocol.hpp" // ВАЖНО: Подключаем бинарный протокол
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>

PointerDaemonClient::PointerDaemonClient() {}

PointerDaemonClient::~PointerDaemonClient() {
    disconnect();
}

std::string get_default_socket_path() {
    const char* xr = std::getenv("XDG_RUNTIME_DIR");
    if (xr && xr[0]) return std::string(xr) + "/evdev-pointer.sock";
    uid_t uid = getuid();
    return std::string("/tmp/evdev-pointer-") + std::to_string(uid) + ".sock";
}

bool PointerDaemonClient::connect(const std::string& path) {
    if (connected) disconnect();

    socket_path = path.empty() ? get_default_socket_path() : path;
    
    sockfd = socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    if (sockfd < 0) {
        std::cerr << "Failed to create socket: " << strerror(errno) << std::endl;
        return false;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);
    unlink(socket_path.c_str()); 
    
    if (bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "Failed to bind socket to path " << socket_path << ": " << strerror(errno) << std::endl;
        close(sockfd);
        sockfd = -1;
        return false;
    }
    
    connected = true;
    std::cout << "Pointer daemon client is listening on (epoll mode): " << socket_path << std::endl;
    return true;
}

void PointerDaemonClient::disconnect() {
    connected = false;
    if (sockfd >= 0) {
        close(sockfd);
        unlink(socket_path.c_str());
        sockfd = -1;
    }
}

void PointerDaemonClient::set_callbacks(MotionCallback motion_cb, MoveCallback move_cb, ClickCallback click_cb) {
    on_motion = std::move(motion_cb);
    on_move = std::move(move_cb);
    on_click = std::move(click_cb);
}

// БИНАРНОЕ ЧТЕНИЕ (Zero-overhead)
void PointerDaemonClient::process_pending_data() {
    if (sockfd < 0 || !connected) return;

    PointerDatagram datagram;
    
    while (true) {
        // Читаем ровно размер нашей структуры
        ssize_t n = recv(sockfd, &datagram, sizeof(PointerDatagram), 0);
        
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break; // Буфер пуст
            std::cerr << "Pointer receive error: " << strerror(errno) << std::endl;
            break;
        } 
        
        // Проверяем, что прочитали правильную структуру (сравниваем магическое число)
        if (n == sizeof(PointerDatagram) && datagram.magic == POINTER_MAGIC) {
            if (on_motion) {
                // Передаем нулевые скорости (vx, vy) и dt, так как вся интерполяция
                // теперь должна делаться в самих эффектах, мышь шлет только дельту.
                on_motion(datagram.dx, datagram.dy, 0.0, 0.0, 0.016, datagram.is_touchpad, "evdev");
            }
        }
    }
}