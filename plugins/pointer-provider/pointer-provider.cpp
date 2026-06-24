#include "data-provider.hpp"
#include "ipc-protocol.hpp" // Скопированный заголовок с PointerDatagram
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <iostream>
#include <cstring>
#include <cstdlib>

class PointerProvider : public IDataProvider {
    float* p_dx = nullptr;
    float* p_dy = nullptr;
    int sockfd = -1;
    std::string socket_path;

public:
    bool initialize(ICoreContext* core) override {
        // 1. Биндим переменные в BlackBoard
        p_dx = core->get_blackboard().bind_float("mouse.dx");
        p_dy = core->get_blackboard().bind_float("mouse.dy");

        // 2. Создаем UNIX сокет
        const char* xr = std::getenv("XDG_RUNTIME_DIR");
        socket_path = xr ? std::string(xr) + "/evdev-pointer.sock" : "/tmp/evdev-pointer.sock";
        
        sockfd = socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK, 0);
        if (sockfd < 0) return false;

        struct sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);
        unlink(socket_path.c_str());
        
        if (bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            close(sockfd);
            return false;
        }

        // 3. Отдаем дескриптор сокета Ядру
        core->register_epoll_fd(sockfd, [this](uint32_t) { this->on_data_ready(); });
        
        std::cout << "[Provider] Pointer Provider started on " << socket_path << std::endl;
        return true;
    }

    void on_data_ready() {
        PointerDatagram datagram;
        while (recv(sockfd, &datagram, sizeof(datagram), 0) == sizeof(datagram)) {
            if (datagram.magic == POINTER_MAGIC) {
                // Прямая запись в память (Zero-Latency)
                *p_dx += static_cast<float>(datagram.dx);
                *p_dy += static_cast<float>(datagram.dy);
            }
        }
    }

    void cleanup() override {
        if (sockfd >= 0) { close(sockfd); unlink(socket_path.c_str()); }
    }
    const char* get_name() const override { return "Evdev Pointer Provider"; }
};

// Экспорт плагина
extern "C" {
    IDataProvider* create_provider() { return new PointerProvider(); }
    void destroy_provider(IDataProvider* p) { delete p; }
}