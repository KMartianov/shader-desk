#include "data-provider.hpp"
#include "ipc-protocol.hpp"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <iostream>
#include <cstring>
#include <cerrno>

class PointerProvider : public IDataProvider {
    float* p_accum_x = nullptr;
    float* p_accum_y = nullptr;
    int sockfd = -1;

    // --- Управляемые параметры (из Lua) ---
    float mouse_sensitivity = 1.0f;
    float touchpad_sensitivity = 3.0f;
    bool invert_x = false;
    bool invert_y = false;

public:
    const char* get_name() const override { return "Evdev Pointer Provider"; }

    // --- Реализация нового интерфейса параметров ---
    std::vector<EffectParameter> get_parameters() const override {
        return {
            {"mouse_sensitivity", "Чувствительность обычной мыши", mouse_sensitivity},
            {"touchpad_sensitivity", "Чувствительность тачпада (абс. координаты)", touchpad_sensitivity},
            {"invert_x", "Инвертировать движение по оси X", invert_x},
            {"invert_y", "Инвертировать движение по оси Y", invert_y}
        };
    }

    void set_parameter(const std::string& name, const EffectParameterValue& value) override {
        try {
            if (name == "mouse_sensitivity") mouse_sensitivity = std::get<float>(value);
            else if (name == "touchpad_sensitivity") touchpad_sensitivity = std::get<float>(value);
            else if (name == "invert_x") invert_x = std::get<bool>(value);
            else if (name == "invert_y") invert_y = std::get<bool>(value);
        } catch (const std::bad_variant_access& e) {
            std::cerr << "[PointerProvider] Type mismatch for parameter '" << name << "'" << std::endl;
        }
    }

    bool initialize(ICoreContext* core) override {
        p_accum_x = core->get_blackboard().bind_float("mouse.accum_x");
        p_accum_y = core->get_blackboard().bind_float("mouse.accum_y");

        sockfd = socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK, 0);
        if (sockfd < 0) return false;

        struct sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        const char* socket_name = "shader-desk-pointer";
        
        addr.sun_path[0] = '\0';
        strncpy(&addr.sun_path[1], socket_name, sizeof(addr.sun_path) - 2);
        socklen_t addr_len = sizeof(sa_family_t) + 1 + strlen(socket_name);
        
        if (bind(sockfd, (struct sockaddr*)&addr, addr_len) < 0) {
            close(sockfd);
            return false;
        }

        core->register_epoll_fd(sockfd, [this](uint32_t) { this->on_data_ready(); });
        return true;
    }

    void on_data_ready() {
        PointerDatagram datagram;
        while (true) {
            ssize_t bytes_read = recv(sockfd, &datagram, sizeof(datagram), MSG_DONTWAIT);
            if (bytes_read < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                if (errno == EINTR) continue;
                break;
            }

            if (bytes_read == sizeof(PointerDatagram) && datagram.magic == POINTER_MAGIC) {
                // --- SMART PROVIDER ЛОГИКА ---
                // Применяем чувствительность и инверсию до записи в BlackBoard
                float sens = datagram.is_touchpad ? touchpad_sensitivity : mouse_sensitivity;
                float dx = datagram.dx * sens * (invert_x ? -1.0f : 1.0f);
                float dy = datagram.dy * sens * (invert_y ? -1.0f : 1.0f);

                *p_accum_x += dx;
                *p_accum_y += dy;
            }
        }
    }

    void cleanup() override {
        if (sockfd >= 0) close(sockfd); 
    }
};

extern "C" {
    IDataProvider* create_provider() { return new PointerProvider(); }
    void destroy_provider(IDataProvider* p) { delete p; }
}