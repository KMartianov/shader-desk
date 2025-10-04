// src/audio-daemon-client.hpp

#pragma once

#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include <functional>
#include <nlohmann/json.hpp>
#include <sys/un.h>
#include "audio-data.hpp" // <--- ДОБАВЛЕНО


class AudioDaemonClient {
public:
    using AudioDataCallback = std::function<void(const AudioData&)>;

    AudioDaemonClient();
    ~AudioDaemonClient();

    // Запрещаем копирование и перемещение
    AudioDaemonClient(const AudioDaemonClient&) = delete;
    AudioDaemonClient& operator=(const AudioDaemonClient&) = delete;

    // Подключается к сокету и запускает поток прослушивания
    bool connect_and_listen(const std::string& socket_path);

    // Устанавливает колбэк для обработки полученных данных
    void set_callback(AudioDataCallback cb);

    // Останавливает поток и отключается
    void disconnect();

private:
    void listen_loop();
    bool parse_message(const char* buffer, size_t len, AudioData& out_data);

    std::atomic<bool> running_{false};
    std::thread listener_thread_{};
    int sockfd_ = -1;
    struct sockaddr_un server_addr_{};
    std::string socket_path_;

    AudioDataCallback callback_{nullptr};
};