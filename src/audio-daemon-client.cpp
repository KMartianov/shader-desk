// src/audio-daemon-client.cpp

#include "audio-daemon-client.hpp"
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <cerrno>

#include "utils.hpp"

using json = nlohmann::json;

AudioDaemonClient::AudioDaemonClient() = default;

AudioDaemonClient::~AudioDaemonClient() {
    disconnect();
}

void AudioDaemonClient::set_callback(AudioDataCallback cb) {
    callback_ = std::move(cb);
}

bool AudioDaemonClient::connect_and_listen(const std::string& socket_path) {
    socket_path_ = socket_path;

    if ((sockfd_ = socket(AF_UNIX, SOCK_DGRAM, 0)) < 0) {
        std::cerr << "AudioClient: Failed to create socket: " << strerror(errno) << std::endl;
        return false;
    }

    // Устанавливаем таймаут на получение данных, чтобы не блокироваться вечно
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    if (setsockopt(sockfd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        std::cerr << "AudioClient: Failed to set socket timeout: " << strerror(errno) << std::endl;
        // Не фатальная ошибка, можно продолжить
    }

    memset(&server_addr_, 0, sizeof(server_addr_));
    server_addr_.sun_family = AF_UNIX;
    strncpy(server_addr_.sun_path, socket_path_.c_str(), sizeof(server_addr_.sun_path) - 1);
    
    // 1. Удаляем старый файл сокета, если он существует. Это предотвращает ошибку "Address already in use".
    unlink(socket_path_.c_str());

    // 2. Привязываем сокет к пути в файловой системе, чтобы он мог получать данные.
    if (bind(sockfd_, (struct sockaddr*)&server_addr_, sizeof(server_addr_)) < 0) {
        std::cerr << "AudioClient: Failed to bind socket to " << socket_path_ << ": " << strerror(errno) << std::endl;
        close(sockfd_);
        sockfd_ = -1;
        return false;
    }
    
    running_ = true;
    listener_thread_ = std::thread(&AudioDaemonClient::listen_loop, this);

    std::cout << "AudioClient: Started listening on socket: " << socket_path << std::endl;
    return true;
}


void AudioDaemonClient::disconnect() {
    running_ = false;
    if (listener_thread_.joinable()) {
        listener_thread_.join();
    }
    if (sockfd_ != -1) {
        close(sockfd_);
        // Удаляем файл сокета при штатном завершении
        unlink(socket_path_.c_str());
        sockfd_ = -1;
    }
}

void AudioDaemonClient::listen_loop() {
    char buffer[4096]; // Буфер для JSON-сообщений
    AudioData audio_data;

    while (running_) {
        // Мы используем sendto в демоне, но здесь мы должны читать.
        // Поскольку это DGRAM, мы не делаем accept(). Просто recvfrom().
        ssize_t n = recvfrom(sockfd_, buffer, sizeof(buffer) - 1, 0, nullptr, nullptr);

        if (n > 0) {
            buffer[n] = '\0';
            if (parse_message(buffer, n, audio_data)) {
                if (callback_) {
                    callback_(audio_data);
                }
            }
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Таймаут, это нормально. Просто продолжаем.
                continue;
            }
            if (!running_) break; // Выход по флагу
            std::cerr << "AudioClient: recvfrom error: " << strerror(errno) << std::endl;
            // Можно добавить логику переподключения
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
}

bool AudioDaemonClient::parse_message(const char* buffer, size_t len, AudioData& out_data) {
    try {
        json j = json::parse(buffer, buffer + len);
        out_data.volume = j.value("volume", 0.0);
        out_data.bass = j.value("bass", 0.0);
        out_data.mid = j.value("mid", 0.0);
        out_data.treble = j.value("treble", 0.0);
        if (j.contains("bands") && j["bands"].is_array()) {
            out_data.bands = j["bands"].get<std::vector<double>>();
        }
        return true;
    } catch (const json::parse_error& e) {
        std::cerr << "AudioClient: JSON parse error: " << e.what() << std::endl;
        return false;
    }
}