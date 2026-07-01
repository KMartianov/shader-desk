#include "data-provider.hpp"
#include "audio-data.hpp" 
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <iostream>
#include <cstring>
#include <cerrno>
#include <algorithm>
#include <cmath>

class AudioProvider : public IDataProvider {
    // --- Указатели на память ядра (BlackBoard) ---
    float* p_volume = nullptr;
    float* p_bass = nullptr;
    float* p_mid = nullptr;
    float* p_treble = nullptr;
    float* p_bands = nullptr; 

    int sockfd = -1;
    ICoreContext* m_core = nullptr;

    // --- Управляемые параметры (из Lua) ---
    float smoothing = 0.85f; // Скорость падения (Decay)
    float volume_multiplier = 1.0f;
    float bass_multiplier = 1.0f;
    float mid_multiplier = 1.0f;
    float treble_multiplier = 1.0f;

public:
    const char* get_name() const override { return "Cava Audio Provider"; }

    std::vector<EffectParameter> get_parameters() const override {
        return {
            {"smoothing", "Сглаживание спада/инерция (0.0 - 0.99)", smoothing},
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
        if (sockfd >= 0) return true; // Защита от Hot-Reload

        m_core = core;
        
        // Привязываем переменные к центральной шине
        p_volume = core->get_blackboard()->bind_float("audio.volume");
        p_bass   = core->get_blackboard()->bind_float("audio.bass");
        p_mid    = core->get_blackboard()->bind_float("audio.mid");
        p_treble = core->get_blackboard()->bind_float("audio.treble");
        p_bands  = core->get_blackboard()->bind_float_array("audio.bands", 64);

        // Создаем неблокирующий UNIX Datagram сокет
        sockfd = socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK, 0);
        if (sockfd < 0) return false;

        struct sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        const char* socket_name = "shader-desk-audio";
        
        // Linux abstract socket namespace (начинается с нулевого байта)
        addr.sun_path[0] = '\0'; 
        strncpy(&addr.sun_path[1], socket_name, sizeof(addr.sun_path) - 2);
        socklen_t addr_len = sizeof(sa_family_t) + 1 + strlen(socket_name);
        
        if (bind(sockfd, (struct sockaddr*)&addr, addr_len) < 0) {
            close(sockfd);
            sockfd = -1;
            return false;
        }

        // Регистрируем сокет в epoll-цикле ядра Wayland (Zero-Latency)
        core->register_epoll_fd(sockfd, [](uint32_t events, void* user_data) {
            static_cast<AudioProvider*>(user_data)->on_data_ready();
        }, this);
        
        return true;
    }

    // Вызывается ядром Linux только тогда, когда в сокете есть новые байты
    void on_data_ready() {
        AudioData datagram;
        AudioData latest_datagram;
        bool has_new_data = false;

        // ВАЖНО: Цикл while крутится, пока в сокете есть данные.
        // MSG_DONTWAIT гарантирует, что мы не зависнем, когда сокет опустеет.
        while (true) {
            ssize_t bytes_read = recv(sockfd, &datagram, sizeof(datagram), MSG_DONTWAIT);
            
            if (bytes_read < 0) {
                // Сокет пуст (мы вычитали всё старье и догнали реальное время)
                if (errno == EAGAIN || errno == EWOULDBLOCK) break; 
                if (errno == EINTR) continue;
                break;
            }

            if (bytes_read == sizeof(AudioData) && datagram.magic == 0x41554431) {
                latest_datagram = datagram; // Перезаписываем старые данные более свежими
                has_new_data = true;
            }
        }

        // Применяем ТОЛЬКО самый свежий пакет (0 задержки)
        if (has_new_data) {
            apply_dynamic_smoothing(*p_volume, latest_datagram.volume, volume_multiplier);
            apply_dynamic_smoothing(*p_bass,   latest_datagram.bass,   bass_multiplier);
            apply_dynamic_smoothing(*p_mid,    latest_datagram.mid,    mid_multiplier);
            apply_dynamic_smoothing(*p_treble, latest_datagram.treble, treble_multiplier);
            
            for (int i = 0; i < 64; ++i) {
                apply_dynamic_smoothing(p_bands[i], latest_datagram.bands[i], volume_multiplier);
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

private:
    // Математически элегантная функция сглаживания
    inline void apply_dynamic_smoothing(float& current, float raw_target, float multiplier) {
        // Умножаем сырое значение и обрезаем, чтобы не выйти за рамки
        float target = std::clamp(raw_target * multiplier, 0.0f, 1.0f);
        
        if (target > current) {
            // FAST ATTACK: Звук стал громче.
            // Реагируем почти мгновенно (на 85% принимаем новое значение).
            // Оставшиеся 15% убирают микро-джиттер цифрового сигнала.
            current = (current * 0.15f) + (target * 0.85f);
        } else {
            // SMOOTH DECAY: Звук стих.
            // Плавно опускаем значение вниз под действием параметра `smoothing` из Lua.
            current = (current * smoothing) + (target * (1.0f - smoothing));
        }
        
        // Защита от денормализованных чисел (аппаратное замедление CPU при float близких к нулю)
        if (current < 1e-5f) current = 0.0f;
    }
};

// --- ABI ЭКСПОРТ ДЛЯ PLUGIN MANAGER ---
extern "C" {
    IDataProviderABI* create_provider() { 
        return new AudioProvider(); 
    }
    void destroy_provider(IDataProviderABI* p) { 
        delete static_cast<AudioProvider*>(p); 
    }
}
