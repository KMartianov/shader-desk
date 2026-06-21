#include "audio-daemon-client.hpp"
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <fcntl.h>

AudioDaemonClient::AudioDaemonClient() = default;

AudioDaemonClient::~AudioDaemonClient() {
    disconnect();
}

void AudioDaemonClient::set_callback(AudioDataCallback cb) {
    callback_ = std::move(cb);
}

bool AudioDaemonClient::connect(const std::string& socket_path) {
    socket_path_ = socket_path;

    if ((sockfd_ = socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK, 0)) < 0) { // Сразу делаем неблокирующим!
        std::cerr << "AudioClient: Failed to create socket: " << strerror(errno) << std::endl;
        return false;
    }

    memset(&server_addr_, 0, sizeof(server_addr_));
    server_addr_.sun_family = AF_UNIX;
    strncpy(server_addr_.sun_path, socket_path_.c_str(), sizeof(server_addr_.sun_path) - 1);
    
    unlink(socket_path_.c_str());

    if (bind(sockfd_, (struct sockaddr*)&server_addr_, sizeof(server_addr_)) < 0) {
        std::cerr << "AudioClient: Failed to bind socket to " << socket_path_ << ": " << strerror(errno) << std::endl;
        close(sockfd_);
        sockfd_ = -1;
        return false;
    }

    std::cout << "AudioClient: Ready on socket (epoll mode): " << socket_path << std::endl;
    return true;
}

void AudioDaemonClient::disconnect() {
    if (sockfd_ != -1) {
        close(sockfd_);
        unlink(socket_path_.c_str());
        sockfd_ = -1;
    }
}

// НОВЫЙ МЕТОД: Читает все доступные данные без блокировки
void AudioDaemonClient::process_pending_data() {
    if (sockfd_ < 0) return;

    AudioData audio_data;
    AudioData latest_data;
    bool got_data = false;

    while (true) {
        ssize_t n = recvfrom(sockfd_, &audio_data, sizeof(AudioData), 0, nullptr, nullptr);
        if (n < 0) break;
        if (n == sizeof(AudioData) && audio_data.magic == 0x41554431) {
            latest_data = audio_data; // Запоминаем только самый свежий фрейм
            got_data = true;
        }
    }

    if (got_data && callback_) {
        callback_(latest_data);
    }
}