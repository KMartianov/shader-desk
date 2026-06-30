#include "data-provider.hpp"
#include "audio-data.hpp" 
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <iostream>
#include <cstring>
#include <cerrno>
#include <algorithm>

class AudioProvider : public IDataProvider {
    float* p_volume = nullptr;
    float* p_bass = nullptr;
    float* p_mid = nullptr;
    float* p_treble = nullptr;
    float* p_bands = nullptr; 

    int sockfd = -1;
    ICoreContext* m_core = nullptr;

    // --- Управляемые параметры (из Lua) ---
    float smoothing = 0.85f;
    float volume_multiplier = 1.0f;
    float bass_multiplier = 1.0f;
    float mid_multiplier = 1.0f;
    float treble_multiplier = 1.0f;

public:
    const char* get_name() const override { return "Cava Audio Provider"; }

    // --- Реализация нового интерфейса параметров ---
    std::vector<EffectParameter> get_parameters() const override {
        return {
            {"smoothing", "Сглаживание/инерция (0.0 - 0.99)", smoothing},
            {"volume_multiplier", "Множитель общей громкости", volume_multiplier},
            {"bass_multiplier", "Множитель баса", bass_multiplier},
            {"mid_multiplier", "Множитель средних частот", mid_multiplier},
            {"treble_multiplier", "Множитель высоких частот", treble_multiplier}
        };
    }

    void set_parameter(const std::string& name, const EffectParameterValue& value) override {
        try {
            if (name == "smoothing") smoothing = std::clamp(std::get<float>(value), 0.0f, 0.99f);
            else if (name == "volume_multiplier") volume_multiplier = std::get<float>(value);
            else if (name == "bass_multiplier") bass_multiplier = std::get<float>(value);
            else if (name == "mid_multiplier") mid_multiplier = std::get<float>(value);
            else if (name == "treble_multiplier") treble_multiplier = std::get<float>(value);
        } catch (const std::bad_variant_access& e) {
            std::cerr << "[AudioProvider] Type mismatch for parameter '" << name << "'" << std::endl;
        }
    }

    bool initialize(ICoreContext* core) override {
        // Защита от Hot-Reload
        if (sockfd >= 0) return true;

        m_core = core;
        p_volume = core->get_blackboard().bind_float("audio.volume");
        p_bass   = core->get_blackboard().bind_float("audio.bass");
        p_mid    = core->get_blackboard().bind_float("audio.mid");
        p_treble = core->get_blackboard().bind_float("audio.treble");
        p_bands  = core->get_blackboard().bind_float_array("audio.bands", 64);

        sockfd = socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK, 0);
        if (sockfd < 0) return false;

        struct sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        const char* socket_name = "shader-desk-audio";
        
        addr.sun_path[0] = '\0'; 
        strncpy(&addr.sun_path[1], socket_name, sizeof(addr.sun_path) - 2);
        socklen_t addr_len = sizeof(sa_family_t) + 1 + strlen(socket_name);
        
        if (bind(sockfd, (struct sockaddr*)&addr, addr_len) < 0) {
            close(sockfd);
            sockfd = -1;
            return false;
        }

        core->register_epoll_fd(sockfd, [this](uint32_t) { this->on_data_ready(); });
        return true;
    }

    void on_data_ready() {
        AudioData datagram;
        while (true) {
            ssize_t bytes_read = recv(sockfd, &datagram, sizeof(datagram), MSG_DONTWAIT);
            if (bytes_read < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) break; 
                if (errno == EINTR) continue; 
                break;
            }

            if (bytes_read == sizeof(AudioData) && datagram.magic == 0x41554431) {
                // --- SMART PROVIDER ЛОГИКА ---
                // Читаем текущие сглаженные значения из BlackBoard и применяем формулу:
                // Новое = (Старое * Сглаживание) + (Сырое * Множитель * (1 - Сглаживание))
                
                float inv = 1.0f - smoothing;

                *p_volume = (smoothing * (*p_volume)) + (inv * std::clamp(datagram.volume * volume_multiplier, 0.0f, 1.0f));
                *p_bass   = (smoothing * (*p_bass))   + (inv * std::clamp(datagram.bass * bass_multiplier, 0.0f, 1.0f));
                *p_mid    = (smoothing * (*p_mid))    + (inv * std::clamp(datagram.mid * mid_multiplier, 0.0f, 1.0f));
                *p_treble = (smoothing * (*p_treble)) + (inv * std::clamp(datagram.treble * treble_multiplier, 0.0f, 1.0f));
                
                for (int i = 0; i < 64; ++i) {
                    p_bands[i] = (smoothing * p_bands[i]) + (inv * std::clamp(datagram.bands[i] * volume_multiplier, 0.0f, 1.0f));
                }
            }
        }
    }

    void cleanup() override {
        if (sockfd >= 0) {
            if (m_core) m_core->unregister_epoll_fd(sockfd);
            close(sockfd);
            sockfd = -1;
        }
    }

};

extern "C" {
    IDataProvider* create_provider() { return new AudioProvider(); }
    void destroy_provider(IDataProvider* p) { delete p; }
}