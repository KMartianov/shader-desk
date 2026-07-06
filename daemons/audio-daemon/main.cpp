// src/audio-daemon/main.cpp
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

#include "audio-data.hpp"

// --- Configuration ---
const int SAMPLE_RATE = 44100;
const int READ_FRAMES = 512;      // ~11ms задержки (КЛЮЧ К НУЛЕВОЙ ЗАДЕРЖКЕ)
const int FFT_SIZE = 2048;        // Размер окна Фурье (дает хорошее разрешение для баса)
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

// Утилита для выполнения shell команд
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

// Автоматический поиск монитора по умолчанию в PipeWire/PulseAudio
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

// Генератор окна Ханна (для сглаживания краев аудио-сэмпла)
static std::vector<double> make_hann(int N) {
    std::vector<double> w(N);
    for (int n = 0; n < N; ++n) {
        w[n] = 0.5 * (1.0 - cos(2.0 * M_PI * n / (N - 1)));
    }
    return w;
}

// Логарифмическая шкала частот (от 20Hz до 20000Hz)
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

    // --- НАСТРОЙКА PULSEAUDIO / PIPEWIRE ---
    pa_sample_spec ss;
    ss.format = PA_SAMPLE_FLOAT32LE;
    ss.channels = 2; // Читаем стерео, микшируем в моно сами
    ss.rate = SAMPLE_RATE;

    // СЕКРЕТ №1: Агрессивный микро-буферинг (Как в Cava)
    pa_buffer_attr pb;
    pb.maxlength = (uint32_t)-1;
    pb.tlength   = (uint32_t)-1;
    pb.prebuf    = (uint32_t)-1;
    pb.minreq    = (uint32_t)-1;
    // Просим сервер отдавать звук порциями строго по 512 сэмплов (~11мс)
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

    // --- НАСТРОЙКА UNIX-СОКЕТА ---
    int sockfd = socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    const char* socket_name = "shader-desk-audio";
    addr.sun_path[0] = '\0'; // Abstract socket namespace
    strncpy(&addr.sun_path[1], socket_name, sizeof(addr.sun_path) - 2);
    socklen_t addr_len = sizeof(sa_family_t) + 1 + strlen(socket_name);

    // --- НАСТРОЙКА FFTW ---
    std::vector<double> audio_history(FFT_SIZE, 0.0);
    std::vector<double> fft_in(FFT_SIZE, 0.0);
    std::vector<fftw_complex> fft_out(FFT_RESULT_SIZE);
    
    // Используем FFTW_MEASURE один раз при запуске для максимальной скорости в цикле
    fftw_plan plan = fftw_plan_dft_r2c_1d(FFT_SIZE, fft_in.data(), fft_out.data(), FFTW_ESTIMATE);
    std::vector<double> win = make_hann(FFT_SIZE);
    std::vector<double> band_edges = make_log_frequencies(NUM_BANDS, 20.0, SAMPLE_RATE / 2.0);

    std::vector<float> read_buf(READ_FRAMES * ss.channels);

    // Глобальный пик громкости (нужен для нормализации спектра, чтобы тихая музыка тоже шевелила сферу)
    double normalization_peak = 0.001; 

    // --- ГЛАВНЫЙ ЦИКЛ (ZERO-LATENCY) ---
    while (running) {
        // 1. Блокирующее чтение микро-порции из PipeWire (ждет ~11 миллисекунд)
        if (pa_simple_read(s, read_buf.data(), read_buf.size() * sizeof(float), &error) < 0) {
            std::cerr << "[Daemon] pa_simple_read() failed: " << pa_strerror(error) << std::endl;
            break;
        }

        // 2. Сдвигаем старые данные влево, освобождая место в конце буфера
        std::memmove(audio_history.data(), audio_history.data() + READ_FRAMES, (FFT_SIZE - READ_FRAMES) * sizeof(double));

        // 3. Микшируем стерео в моно и добавляем в конец истории
        double current_rms_sum = 0.0;
        int write_offset = FFT_SIZE - READ_FRAMES;
        for (int i = 0; i < READ_FRAMES; ++i) {
            double sample = (read_buf[i * 2] + read_buf[i * 2 + 1]) * 0.5;
            audio_history[write_offset + i] = sample;
            current_rms_sum += sample * sample;
        }
        
        // Вычисляем мгновенную громкость (Root Mean Square)
        double rms = std::sqrt(current_rms_sum / READ_FRAMES);

        // 4. Применяем окно Ханна и копируем в буфер FFT
        for (int n = 0; n < FFT_SIZE; ++n) {
            fft_in[n] = audio_history[n] * win[n];
        }

        // 5. Выполняем FFT
        fftw_execute(plan);

        // 6. Вычисляем амплитуды (Magnitudes)
        std::vector<double> magnitudes(FFT_RESULT_SIZE);
        double current_peak = 1e-12;
        
        // Начинаем с 1, чтобы пропустить DC-составляющую (смещение 0 Гц)
        for (int k = 1; k < FFT_RESULT_SIZE; ++k) {
            double re = fft_out[k][0];
            double im = fft_out[k][1];
            double mag = std::sqrt(re*re + im*im);
            magnitudes[k] = mag;
            if (mag > current_peak) current_peak = mag;
        }

        // Медленно опускаем пик нормализации (чтобы громкость спектра адаптировалась к треку)
        normalization_peak = std::max(current_peak, normalization_peak * 0.998);

        auto sum_band_range = [&](double f_lo, double f_hi) {
            int bin_lo = freq_to_bin(f_lo);
            int bin_hi = freq_to_bin(f_hi);
            double acc = 0.0;
            for (int bin = bin_lo; bin <= bin_hi; ++bin) acc += magnitudes[bin];
            return (acc / (bin_hi - bin_lo + 1)) / normalization_peak;
        };

        // 7. СЕКРЕТ №3: Формируем пакет RAW-данных БЕЗ СГЛАЖИВАНИЯ!
        AudioData data;
        
        // Мгновенные значения:
        data.volume = static_cast<float>(std::min(1.0, rms * 5.0)); // *5.0 для чувствительности
        data.bass   = static_cast<float>(std::min(1.0, sum_band_range(20.0, 250.0)));
        data.mid    = static_cast<float>(std::min(1.0, sum_band_range(250.0, 4000.0)));
        data.treble = static_cast<float>(std::min(1.0, sum_band_range(4000.0, SAMPLE_RATE / 2.0)));

        for (int b = 0; b < NUM_BANDS; ++b) {
            data.bands[b] = static_cast<float>(std::min(1.0, sum_band_range(band_edges[b], band_edges[b+1])));
        }

        // 8. Отправляем в Wayland-обои не блокируясь
        sendto(sockfd, &data, sizeof(AudioData), MSG_DONTWAIT, (struct sockaddr*)&addr, addr_len);
    }

    // --- Очистка ---
    std::cerr << "[Daemon] Cleaning up..." << std::endl;
    close(sockfd);
    fftw_destroy_plan(plan);
    if (s) pa_simple_free(s);

    return 0;
}