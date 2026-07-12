#include <shader-desk/data-provider.hpp>
#include "audio-data.hpp" 
#include <shader-desk/ipc-utils.hpp>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <iostream>
#include <cstring>
#include <cerrno>
#include <algorithm>
#include <cmath>

class AudioProvider : public IDataProvider {
    // --- Pointers to Core memory (BlackBoard) ---
    float* p_volume = nullptr;
    float* p_bass = nullptr;
    float* p_mid = nullptr;
    float* p_treble = nullptr;
    float* p_bands = nullptr; 

    int sockfd = -1;
    ICoreContext* m_core = nullptr;

    // --- Managed parameters (from Lua) ---
    float smoothing = 0.85f; // Decay speed
    float volume_multiplier = 1.0f;
    float bass_multiplier = 1.0f;
    float mid_multiplier = 1.0f;
    float treble_multiplier = 1.0f;

public:
    const char* get_name() const override { return "Cava Audio Provider"; }

    std::vector<EffectParameter> get_parameters() const override {
        return {
            {"smoothing", "Decay smoothing/inertia (0.0 - 0.99)", smoothing},
            {"volume_multiplier", "Overall volume multiplier", volume_multiplier},
            {"bass_multiplier", "Bass multiplier", bass_multiplier},
            {"mid_multiplier", "Mid frequencies multiplier", mid_multiplier},
            {"treble_multiplier", "Treble multiplier", treble_multiplier}
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
        if (sockfd >= 0) return true; // Hot-Reload protection

        m_core = core;
        
        // Bind variables to the central data bus
        p_volume = core->get_blackboard()->bind_float("audio.volume");
        p_bass   = core->get_blackboard()->bind_float("audio.bass");
        p_mid    = core->get_blackboard()->bind_float("audio.mid");
        p_treble = core->get_blackboard()->bind_float("audio.treble");
        p_bands  = core->get_blackboard()->bind_float_array("audio.bands", 64);

        // Create non-blocking UNIX Datagram socket
        sockfd = socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK, 0);
        if (sockfd < 0) return false;

        struct sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        
        std::string socket_path = shader_desk::get_ipc_socket_path("shader-desk-audio");
        strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);
        
        unlink(socket_path.c_str()); // IMPORTANT: Delete the old file!

        if (bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            std::cerr << "[AudioProvider] Failed to bind socket at " << socket_path << std::endl;
            close(sockfd);
            sockfd = -1;
            return false;
        }
        
    

        // Register socket in Wayland core's epoll loop (Zero-Latency)
        core->register_epoll_fd(sockfd, [](uint32_t events, void* user_data) {
            static_cast<AudioProvider*>(user_data)->on_data_ready();
        }, this);
        
        return true;
    }

    // Called by the Linux kernel ONLY when new bytes are in the socket
    void on_data_ready() {
        AudioDatagram datagram;
        AudioDatagram latest_datagram;
        bool has_new_data = false;

        // IMPORTANT: The while loop runs as long as there is data in the socket.
        // MSG_DONTWAIT ensures we don't block when the socket is empty.
        while (true) {
            ssize_t bytes_read = recv(sockfd, &datagram, sizeof(datagram), MSG_DONTWAIT);
            
            if (bytes_read < 0) {
                // Socket empty (we drained stale data and caught up to real-time)
                if (errno == EAGAIN || errno == EWOULDBLOCK) break; 
                if (errno == EINTR) continue;
                break;
            }

            if (bytes_read == sizeof(AudioDatagram) && datagram.magic == 0x41554431) {
                latest_datagram = datagram; // Overwrite old data with fresher packets
                has_new_data = true;
            }
        }

        // Apply ONLY the freshest packet (0 latency)
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
    // Mathematically elegant smoothing function
    inline void apply_dynamic_smoothing(float& current, float raw_target, float multiplier) {
        // Multiply raw value and clamp to prevent out-of-bounds
        float target = std::clamp(raw_target * multiplier, 0.0f, 1.0f);
        
        if (target > current) {
            // FAST ATTACK: Sound became louder.
            // React almost instantly (accept 85% of the new value).
            // The remaining 15% eliminates digital signal micro-jitter.
            current = (current * 0.15f) + (target * 0.85f);
        } else {
            // SMOOTH DECAY: Sound faded.
            // Smoothly drop the value based on the Lua `smoothing` parameter.
            current = (current * smoothing) + (target * (1.0f - smoothing));
        }
        
        // Protection against denormalized numbers (prevents CPU hardware slowdowns with floats near zero)
        if (current < 1e-5f) current = 0.0f;
    }
};

// --- ABI EXPORT FOR PLUGIN MANAGER ---
extern "C" {

    uint32_t get_abi_version() {
        return SHADER_DESK_ABI_VERSION;
    }
    
    IDataProviderABI* create_provider() { 
        return new AudioProvider(); 
    }
    void destroy_provider(IDataProviderABI* p) { 
        delete static_cast<AudioProvider*>(p); 
    }
}
