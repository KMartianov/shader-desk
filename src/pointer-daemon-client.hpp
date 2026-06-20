// src/pointer-daemon-client.hpp
#pragma once
#include <string>
#include <functional>
#include <sys/socket.h>
#include <sys/un.h>
#include <cstdint>

class PointerDaemonClient {
public:
    using MotionCallback = std::function<void(double dx, double dy, double vx, double vy, double dt, bool normalized, const std::string& device_name)>;
    using MoveCallback = std::function<void(double x, double y)>;
    using ClickCallback = std::function<void(double x, double y, uint32_t button)>;

    PointerDaemonClient();
    ~PointerDaemonClient();

    // Хорошая практика: запрещаем копирование, так как класс владеет системным ресурсом (sockfd)
    PointerDaemonClient(const PointerDaemonClient&) = delete;
    PointerDaemonClient& operator=(const PointerDaemonClient&) = delete;

    bool connect(const std::string& socket_path = "");
    void disconnect();
    bool is_connected() const { return connected; }

    void set_callbacks(MotionCallback motion_cb, MoveCallback move_cb, ClickCallback click_cb);

    // --- НОВЫЕ МЕТОДЫ ДЛЯ EPOLL ---
    int get_fd() const { return sockfd; }
    void process_pending_data();

private:
    
    int sockfd = -1;
    bool connected = false; // Больше не atomic
    
    MotionCallback on_motion;
    MoveCallback on_move;
    ClickCallback on_click;
    
    std::string socket_path;
};