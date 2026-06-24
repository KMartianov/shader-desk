#include "data-provider.hpp"
#include "ipc-protocol.hpp"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <iostream>
#include <cstring>
#include <cerrno>

class PointerProvider : public IDataProvider {
    // ВНИМАНИЕ: Теперь мы используем ключи-накопители, 
    // чтобы мониторы не "воровали" дельты друг у друга.
    float* p_accum_x = nullptr;
    float* p_accum_y = nullptr;
    int sockfd = -1;

public:
    bool initialize(ICoreContext* core) override {
        // 1. Биндим переменные-накопители в BlackBoard
        p_accum_x = core->get_blackboard().bind_float("mouse.accum_x");
        p_accum_y = core->get_blackboard().bind_float("mouse.accum_y");

        // 2. Создаем UNIX UDP сокет
        sockfd = socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK, 0);
        if (sockfd < 0) {
            std::cerr << "[PointerProvider] socket() failed: " << strerror(errno) << std::endl;
            return false;
        }

        // 3. Используем АБСТРАКТНЫЙ сокет (живет только в памяти Ядра, никаких файлов в /tmp/)
        struct sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        const char* socket_name = "shader-desk-pointer";
        
        addr.sun_path[0] = '\0'; // Волшебный нулевой байт делает сокет абстрактным
        strncpy(&addr.sun_path[1], socket_name, sizeof(addr.sun_path) - 2);
        
        // Размер структуры должен точно учитывать длину имени после \0
        socklen_t addr_len = sizeof(sa_family_t) + 1 + strlen(socket_name);
        
        if (bind(sockfd, (struct sockaddr*)&addr, addr_len) < 0) {
            std::cerr << "[PointerProvider] bind() failed: " << strerror(errno) << std::endl;
            close(sockfd);
            return false;
        }

        // 4. Передаем дескриптор в epoll
        core->register_epoll_fd(sockfd, [this](uint32_t) { this->on_data_ready(); });
        
        std::cout << "[PointerProvider] Started on abstract socket '@" << socket_name << "'" << std::endl;
        return true;
    }

    void on_data_ready() {
        PointerDatagram datagram;
        
        // Идеальный цикл чтения из неблокирующего сокета
        while (true) {
            ssize_t bytes_read = recv(sockfd, &datagram, sizeof(datagram), MSG_DONTWAIT);
            
            if (bytes_read < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) break; // Все пакеты вычитаны
                if (errno == EINTR) continue; // Системное прерывание, просто пробуем снова
                
                std::cerr << "[PointerProvider] recv error: " << strerror(errno) << std::endl;
                break;
            }

            if (bytes_read == sizeof(PointerDatagram) && datagram.magic == POINTER_MAGIC) {
                // НАКАПЛИВАЕМ значения в BlackBoard (Zero-Latency)
                *p_accum_x += datagram.dx;
                *p_accum_y += datagram.dy;
            }
        }
    }

    void cleanup() override {
        if (sockfd >= 0) { 
            close(sockfd); 
            // Обрати внимание: unlink() больше не нужен! Абстрактные сокеты очищаются сами.
        }
    }
    
    const char* get_name() const override { return "Evdev Pointer Provider"; }
};

// Экспорт плагина
extern "C" {
    IDataProvider* create_provider() { return new PointerProvider(); }
    void destroy_provider(IDataProvider* p) { delete p; }
}