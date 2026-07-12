// Src/audio-daemon/main.cpp
#include <iostream>
#include <vector>
#include <cmath>
#include <cstring>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>
#include <sstream>
#include <optional>
#include <algorithm>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <pulse/simple.h>
#include <pulse/error.h>
#include <fftw3.h>
#include <shader-desk/ipc-utils.hpp>
#include "audio-data.hpp"

// --- Configuration ---
const int SAMPLE_RATE = 44100;
const int READ_FRAMES = 512;      // ~11ms latency (THE KEY TO ZERO-LATENCY)
const int FFT_SIZE = 2048;        // Fourier window size (provides good resolution for bass)
const int FFT_RESULT_SIZE = FFT_SIZE / 2 + 1;
const int NUM_BANDS = 64;         

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

std::atomic<bool> running(true);

void signal_handler(int signum) {
    std::cerr << "\nCaught signal " << signum << ", shutting down." << std::endl;
    running = false;
}

// Utility to execute shell commands
static std::string run_cmd_collect(const char* cmd) {
    std::string result;
    FILE* pipe = popen(cmd, "r");
    if (!pipe) return result;
    char buffer[1024];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result += buffer;
    }
    pclose(pipe);
    return result;
}

// Automatic search for the default monitor in PipeWire/PulseAudio
static std::optional<std::string> get_default_monitor() {
    std::string out = run_cmd_collect("pactl info 2>/dev/null");
    std::string default_sink;
    std::istringstream iss(out);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.find("Default Sink:") != std::string::npos) {
            default_sink = line.substr(line.find(":") + 1);
            default_sink.erase(0, default_sink.find_first_not_of(" \t"));
            break;
        }
    }
    if (!default_sink.empty()) {
        std::string monitor = default_sink + ".monitor";
        std::string sources = run_cmd_collect("pactl list short sources 2>/dev/null");
        if (sources.find(monitor) != std::string::npos) {
            return monitor;
        }
    }
    return std::nullopt;
}

// Hann window generator (to smooth audio sample edges)
static std::vector<double> make_hann(int N) {
    std::vector<double> w(N);
    for (int n = 0; n < N; ++n) {
        w[n] = 0.5 * (1.0 - cos(2.0 * M_PI * n / (N - 1)));
    }
    return w;
}

// Logarithmic frequency scale (from 20Hz to 20000Hz)
static std::vector<double> make_log_frequencies(int bands, double fmin, double fmax) {
    std::vector<double> freqs(bands + 1);
    double log_min = std::log(fmin);
    double log_max = std::log(fmax);
    for (int i = 0; i <= bands; ++i) {
        freqs[i] = std::exp(log_min + (static_cast<double>(i) / bands) * (log_max - log_min));
    }
    return freqs;
}

inline int freq_to_bin(double freq) {
    return std::clamp(static_cast<int>(freq * FFT_SIZE / SAMPLE_RATE + 0.5), 1, FFT_RESULT_SIZE - 1);
}

int main() {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    std::cerr << "[Daemon] Starting High-Performance Audio Capture..." << std::endl;

    // --- PULSEAUDIO / PIPEWIRE SETUP ---
    pa_sample_spec ss;
    ss.format = PA_SAMPLE_FLOAT32LE;
    ss.channels = 2; // Read stereo, mix to mono ourselves
    ss.rate = SAMPLE_RATE;

    // SECRET #1: Aggressive micro-buffering (Like in Cava)
    pa_buffer_attr pb;
    pb.maxlength = (uint32_t)-1;
    pb.tlength   = (uint32_t)-1;
    pb.prebuf    = (uint32_t)-1;
    pb.minreq    = (uint32_t)-1;
    // Ask the server to provide audio strictly in 512-sample chunks (~11ms)
    pb.fragsize  = READ_FRAMES * sizeof(float) * ss.channels; 

    auto monitor = get_default_monitor();
    const char* device = monitor ? monitor->c_str() : nullptr;

    int error = 0;
    pa_simple *s = pa_simple_new(
        NULL, "InteractiveWallpaper-Audio", PA_STREAM_RECORD, device, 
        "FFT Capture", &ss, NULL, &pb, &error
    );

    if (!s) {
        std::cerr << "[Daemon] Failed to connect to PulseAudio: " << pa_strerror(error) << std::endl;
        return 1;
    }
    std::cerr << "[Daemon] Connected to PulseAudio (" << (device ? device : "default") << ")" << std::endl;

    // --- UNIX SOCKET SETUP ---
    int sockfd = socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    
    std::string socket_path = shader_desk::get_ipc_socket_path("shader-desk-audio");
    strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);
    socklen_t addr_len = sizeof(sa_family_t) + socket_path.length() + 1; // +1 for null-terminator

    // --- FFTW SETUP ---
    std::vector<double> audio_history(FFT_SIZE, 0.0);
    std::vector<double> fft_in(FFT_SIZE, 0.0);
    std::vector<fftw_complex> fft_out(FFT_RESULT_SIZE);
    
    // Use FFTW_MEASURE once at startup for maximum loop speed
    fftw_plan plan = fftw_plan_dft_r2c_1d(FFT_SIZE, fft_in.data(), fft_out.data(), FFTW_ESTIMATE);
    std::vector<double> win = make_hann(FFT_SIZE);
    std::vector<double> band_edges = make_log_frequencies(NUM_BANDS, 20.0, SAMPLE_RATE / 2.0);

    std::vector<float> read_buf(READ_FRAMES * ss.channels);

    // Global volume peak (needed for spectrum normalization so quiet music also moves the sphere)
    double normalization_peak = 0.001; 

    // --- MAIN LOOP (ZERO-LATENCY) ---
    while (running) {
        // 1. Blocking read of a micro-chunk from PipeWire (waits ~11 milliseconds)
        if (pa_simple_read(s, read_buf.data(), read_buf.size() * sizeof(float), &error) < 0) {
            std::cerr << "[Daemon] pa_simple_read() failed: " << pa_strerror(error) << std::endl;
            break;
        }

        // 2. Shift old data to the left, freeing up space at the end of the buffer
        std::memmove(audio_history.data(), audio_history.data() + READ_FRAMES, (FFT_SIZE - READ_FRAMES) * sizeof(double));

        // 3. Mix stereo to mono and append to the end of history
        double current_rms_sum = 0.0;
        int write_offset = FFT_SIZE - READ_FRAMES;
        for (int i = 0; i < READ_FRAMES; ++i) {
            double sample = (read_buf[i * 2] + read_buf[i * 2 + 1]) * 0.5;
            audio_history[write_offset + i] = sample;
            current_rms_sum += sample * sample;
        }
        
        // Calculate instantaneous volume (Root Mean Square)
        double rms = std::sqrt(current_rms_sum / READ_FRAMES);

        // 4. Apply Hann window and copy to FFT buffer
        for (int n = 0; n < FFT_SIZE; ++n) {
            fft_in[n] = audio_history[n] * win[n];
        }

        // 5. Execute FFT
        fftw_execute(plan);

        // 6. Calculate amplitudes (Magnitudes)
        std::vector<double> magnitudes(FFT_RESULT_SIZE);
        double current_peak = 1e-12;
        
        // Start from 1 to skip the DC component (0 Hz offset)
        for (int k = 1; k < FFT_RESULT_SIZE; ++k) {
            double re = fft_out[k][0];
            double im = fft_out[k][1];
            double mag = std::sqrt(re*re + im*im);
            magnitudes[k] = mag;
            if (mag > current_peak) current_peak = mag;
        }

        // Slowly decay the normalization peak (so spectrum volume adapts to the track)
        normalization_peak = std::max(current_peak, normalization_peak * 0.998);

        auto sum_band_range = [&](double f_lo, double f_hi) {
            int bin_lo = freq_to_bin(f_lo);
            int bin_hi = freq_to_bin(f_hi);
            double acc = 0.0;
            for (int bin = bin_lo; bin <= bin_hi; ++bin) acc += magnitudes[bin];
            return (acc / (bin_hi - bin_lo + 1)) / normalization_peak;
        };

        // 7. SECRET #3: Form the RAW data packet WITHOUT SMOOTHING!
        AudioDatagram data;
        
        // Instantaneous values:
        data.volume = static_cast<float>(std::min(1.0, rms * 5.0)); // *5.0 for sensitivity
        data.bass   = static_cast<float>(std::min(1.0, sum_band_range(20.0, 250.0)));
        data.mid    = static_cast<float>(std::min(1.0, sum_band_range(250.0, 4000.0)));
        data.treble = static_cast<float>(std::min(1.0, sum_band_range(4000.0, SAMPLE_RATE / 2.0)));

        for (int b = 0; b < NUM_BANDS; ++b) {
            data.bands[b] = static_cast<float>(std::min(1.0, sum_band_range(band_edges[b], band_edges[b+1])));
        }

        // 8. Send to Wayland wallpapers without blocking
        sendto(sockfd, &data, sizeof(AudioDatagram), MSG_DONTWAIT, (struct sockaddr*)&addr, addr_len);
    }

    // --- Cleanup ---
    std::cerr << "[Daemon] Cleaning up..." << std::endl;
    close(sockfd);
    fftw_destroy_plan(plan);
    if (s) pa_simple_free(s);

    return 0;
}