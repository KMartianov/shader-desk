#include <shader-desk/data-provider.hpp>
#include "pointer-data.hpp"
#include <shader-desk/ipc-utils.hpp>
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

    // --- Managed parameters (from Lua) ---
    float mouse_sensitivity = 1.0f;
    float touchpad_sensitivity = 3.0f;
    bool invert_x = false;
    bool invert_y = false;

    ICoreContext* m_core = nullptr;

public:
    const char* get_name() const override { return "Evdev Pointer Provider"; }

    // --- Implementation of the new parameter interface ---
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
        // Hot-Reload protection: if the socket is already created, just return true.
        // Settings (touchpad_sensitivity, etc.) were already updated via set_parameter().
        if (sockfd >= 0) return true; 

        m_core = core;
        p_accum_x = core->get_blackboard()->bind_float("mouse.accum_x");
        p_accum_y = core->get_blackboard()->bind_float("mouse.accum_y");

        sockfd = socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK, 0);
        if (sockfd < 0) return false;

        struct sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::string socket_path = shader_desk::get_ipc_socket_path("shader-desk-pointer");
        strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);
        
        unlink(socket_path.c_str()); // ВАЖНО: Удаляем старый файл!
        
        if (bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            std::cerr << "[PointerProvider] Failed to bind socket at " << socket_path << std::endl;
            close(sockfd);
            sockfd = -1;
            return false;
        }

        core->register_epoll_fd(sockfd, [](uint32_t events, void* user_data) {
            static_cast<PointerProvider*>(user_data)->on_data_ready();
        }, this);
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
                // --- SMART PROVIDER LOGIC ---
                // Apply sensitivity and inversion BEFORE writing to BlackBoard
                float sens = datagram.is_touchpad ? touchpad_sensitivity : mouse_sensitivity;
                float dx = datagram.dx * sens * (invert_x ? -1.0f : 1.0f);
                float dy = datagram.dy * sens * (invert_y ? -1.0f : 1.0f);

                *p_accum_x += dx;
                *p_accum_y += dy;
            }
        }
    }

    void cleanup() override {
        if (sockfd >= 0) {
            // Mandatory: unregister from epoll, otherwise the Core will leak callbacks!
            if (m_core) m_core->unregister_epoll_fd(sockfd);
            close(sockfd);
            sockfd = -1;
        }
    }

};


extern "C" {

    uint32_t get_abi_version() {
        return SHADER_DESK_ABI_VERSION;
    }
    
    IDataProviderABI* create_provider() { 
        return new PointerProvider(); // (e.g., new AudioProvider())
    }
    void destroy_provider(IDataProviderABI* p) { 
        delete static_cast<IDataProvider*>(p); 
    }
}