#include "data-provider.hpp"
#include "audio-data.hpp" // Скопированный заголовок с AudioData
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <iostream>
#include <cstring>
#include <cstdlib>

class AudioProvider : public IDataProvider {
    float* p_volume = nullptr;
    float* p_bass = nullptr;
    float* p_mid = nullptr;
    float* p_treble = nullptr;
    float* p_bands = nullptr; // Указатель на массив из 64 float

    int sockfd = -1;
    std::string socket_path;

public:
    bool initialize(ICoreContext* core) override {
        // 1. Биндим переменные
        p_volume = core->get_blackboard().bind_float("audio.volume");
        p_bass   = core->get_blackboard().bind_float("audio.bass");
        p_mid    = core->get_blackboard().bind_float("audio.mid");
        p_treble = core->get_blackboard().bind_float("audio.treble");
        p_bands  = core->get_blackboard().bind_float_array("audio.bands", 64);

        // 2. Создаем UNIX сокет
        const char* xr = std::getenv("XDG_RUNTIME_DIR");
        socket_path = xr ? std::string(xr) + "/interactive-wallpaper-audio.sock" 
                         : "/tmp/interactive-wallpaper-audio.sock";
        
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
        
        std::cout << "[Provider] Audio Provider started on " << socket_path << std::endl;
        return true;
    }

    void on_data_ready() {
        AudioData datagram;
        while (recv(sockfd, &datagram, sizeof(datagram), 0) == sizeof(datagram)) {
            if (datagram.magic == 0x41554431) {
                // Прямая запись в память (Zero-Latency)
                *p_volume = datagram.volume;
                *p_bass   = datagram.bass;
                *p_mid    = datagram.mid;
                *p_treble = datagram.treble;
                // Копируем спектр в BlackBoard
                std::memcpy(p_bands, datagram.bands, 64 * sizeof(float));
            }
        }
    }

    void cleanup() override {
        if (sockfd >= 0) { close(sockfd); unlink(socket_path.c_str()); }
    }
    const char* get_name() const override { return "Cava Audio Provider"; }
};

// Экспорт плагина
extern "C" {
    IDataProvider* create_provider() { return new AudioProvider(); }
    void destroy_provider(IDataProvider* p) { delete p; }
}