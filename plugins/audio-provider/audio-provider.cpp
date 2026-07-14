// Src/plugins/audio-provider/audio-provider.cpp
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
    // ==============================================================================
    // BLACKBOARD MEMORY POINTERS
    // Direct, zero-overhead access to the Wayland Microkernel's shared memory bus.
    // ==============================================================================
    float* p_volume = nullptr;
    float* p_bass = nullptr;
    float* p_mid = nullptr;
    float* p_treble = nullptr;
    float* p_bands = nullptr; 
    
    // The EQ curve is strictly driven by the Control Plane (Lua scripts).
    // The C++ provider only reads these multipliers to adjust the final spectrum.
    float* p_eq_curve = nullptr; 

    int sockfd = -1;
    ICoreContext* m_core = nullptr;

    // ==============================================================================
    // MANAGED PARAMETERS (Exposed to Lua)
    // ==============================================================================
    float smoothing = 0.85f; // Decay inertia (0.0 = instant, 0.99 = very slow)
    float volume_multiplier = 1.0f;
    float bass_multiplier = 1.0f;
    float mid_multiplier = 1.0f;
    float treble_multiplier = 1.0f;

public:
    const char* get_name() const override { return "Native FFTW Audio Provider"; }

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
        // Hot-Reload protection: prevent socket recreation if the plugin is just
        // receiving updated parameters from Lua during runtime.
        if (sockfd >= 0) return true; 

        m_core = core;
        
        // 1. Map pointers to the BlackBoard. 
        // If the keys don't exist yet, the Core will safely allocate them.
        p_volume = core->get_blackboard()->bind_float("audio.volume");
        p_bass   = core->get_blackboard()->bind_float("audio.bass");
        p_mid    = core->get_blackboard()->bind_float("audio.mid");
        p_treble = core->get_blackboard()->bind_float("audio.treble");
        p_bands  = core->get_blackboard()->bind_float_array("audio.bands", 64);
        
        // Map the EQ curve. We do NOT initialize it with 1.0f here. 
        // We trust the Lua engine to populate this array. If Lua hasn't set it yet, 
        // the default 0.0f from the BlackBoard will safely silence the spectrum 
        // until the Control Plane wakes up, adhering to the Data-Driven design.
        p_eq_curve = core->get_blackboard()->bind_float_array("audio.eq_curve", 64);

        // 2. Create the non-blocking IPC Datagram socket
        sockfd = socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if (sockfd < 0) return false;

        struct sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        
        std::string socket_path = shader_desk::get_ipc_socket_path("shader-desk-audio");
        strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);
        
        // Zombie socket protection: clean up potential leftovers from a crashed session
        unlink(socket_path.c_str()); 

        if (bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            std::cerr << "[AudioProvider] Failed to bind IPC socket at " << socket_path << std::endl;
            close(sockfd);
            sockfd = -1;
            return false;
        }
        
        // 3. Register the socket into the Wayland Epoll loop for Zero-Latency wakeups
        core->register_epoll_fd(sockfd, [](uint32_t events, void* user_data) {
            static_cast<AudioProvider*>(user_data)->on_data_ready();
        }, this);
        
        return true;
    }

    // ==============================================================================
    // HOT PATH (Zero-Allocation & Zero-Latency)
    // Triggered by the Linux kernel exclusively when new bytes hit the socket.
    // ==============================================================================
    void on_data_ready() {
        AudioDatagram datagram;
        AudioDatagram latest_datagram;
        bool has_new_data = false;

        // DRAIN PATTERN: Continuously read from the socket until the OS kernel
        // signals EAGAIN/EWOULDBLOCK. This guarantees we discard stale backlog 
        // packets and extract only the absolute freshest audio frame.
        while (true) {
            ssize_t bytes_read = recv(sockfd, &datagram, sizeof(datagram), MSG_DONTWAIT);
            
            if (bytes_read < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) break; // Socket drained successfully
                if (errno == EINTR) continue; // Interrupted by system signal, retry
                break; // Unrecoverable socket error
            }

            if (bytes_read == sizeof(AudioDatagram) && datagram.magic == 0x41554431) {
                latest_datagram = datagram;
                has_new_data = true;
            }
        }

        // Apply processing strictly to the freshest packet in O(1) time
        if (has_new_data) {
            apply_dynamic_smoothing(*p_volume, latest_datagram.volume, volume_multiplier);
            apply_dynamic_smoothing(*p_bass,   latest_datagram.bass,   bass_multiplier);
            apply_dynamic_smoothing(*p_mid,    latest_datagram.mid,    mid_multiplier);
            apply_dynamic_smoothing(*p_treble, latest_datagram.treble, treble_multiplier);
            
            // Apply Lua-driven EQ curve dynamically without any string parsing or allocations
            for (int i = 0; i < 64; ++i) {
                float eq_multiplier = volume_multiplier * p_eq_curve[i];
                apply_dynamic_smoothing(p_bands[i], latest_datagram.bands[i], eq_multiplier);
            }
        }
    }

    void cleanup() override {
        if (sockfd >= 0) {
            // Strictly unregister from epoll to prevent Use-After-Free crashes in the Core
            if (m_core) m_core->unregister_epoll_fd(sockfd);
            close(sockfd);
            sockfd = -1;
        }
    }

private:
    // ==============================================================================
    // ASYMMETRIC SIGNAL SMOOTHING
    // Ensures explosive visual reaction to beats (Fast Attack), while providing
    // visually pleasing fade-outs (Smooth Decay) governed by the Lua configuration.
    // ==============================================================================
    inline void apply_dynamic_smoothing(float& current, float raw_target, float multiplier) {
        float target = std::clamp(raw_target * multiplier, 0.0f, 1.0f);
        
        if (target > current) {
            // FAST ATTACK: The signal increased.
            // Rapidly adapt (85% weight to the new value). The remaining 15% 
            // filters out harsh digital micro-jitters without introducing perceived lag.
            current = (current * 0.15f) + (target * 0.85f);
        } else {
            // SMOOTH DECAY: The signal decreased.
            // Fall off gradually based on the user-defined `smoothing` inertia.
            current = (current * smoothing) + (target * (1.0f - smoothing));
        }
        
        // Denormalization protection: Prevents extreme CPU penalties when floating 
        // point numbers decay infinitesimally close to zero.
        if (current < 1e-5f) current = 0.0f;
    }
};

// ==============================================================================
// C-ABI EXPORT (Plugin Manager Boundary)
// ==============================================================================
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