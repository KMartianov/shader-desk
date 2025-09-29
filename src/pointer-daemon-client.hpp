// pointer-daemon-client.hpp
#pragma once
#include <string>
#include <functional>
#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>
#include <atomic>
#include <thread>

class PointerDaemonClient {
public:
    using MotionCallback = std::function<void(double dx, double dy, double vx, double vy, double dt, bool normalized, const std::string& device_name)>;
    using MoveCallback = std::function<void(double x, double y)>;
    using ClickCallback = std::function<void(double x, double y, uint32_t button)>;

    PointerDaemonClient();
    ~PointerDaemonClient();

    bool connect(const std::string& socket_path = "");
    void disconnect();
    bool is_connected() const { return connected; }

    void set_callbacks(MotionCallback motion_cb, MoveCallback move_cb, ClickCallback click_cb);

private:
    void receive_loop();
    void parse_message(const std::string& json_msg);
    
    int sockfd = -1;
    std::atomic<bool> connected{false};
    std::atomic<bool> running{false};
    std::thread receiver_thread;
    
    MotionCallback on_motion;
    MoveCallback on_move;
    ClickCallback on_click;
    
    std::string socket_path;
};