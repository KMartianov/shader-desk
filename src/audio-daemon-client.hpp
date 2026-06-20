// Замени содержимое на:
#pragma once
#include <string>
#include <functional>
#include <sys/un.h>
#include "audio-data.hpp"

class AudioDaemonClient {
public:
    using AudioDataCallback = std::function<void(const AudioData&)>;

    AudioDaemonClient();
    ~AudioDaemonClient();

    AudioDaemonClient(const AudioDaemonClient&) = delete;
    AudioDaemonClient& operator=(const AudioDaemonClient&) = delete;

    bool connect(const std::string& socket_path);
    void disconnect();
    void set_callback(AudioDataCallback cb);

    // НОВЫЕ МЕТОДЫ:
    int get_fd() const { return sockfd_; }
    void process_pending_data(); // Вызывается из epoll

private:
    int sockfd_ = -1;
    struct sockaddr_un server_addr_{};
    std::string socket_path_;
    AudioDataCallback callback_{nullptr};
};