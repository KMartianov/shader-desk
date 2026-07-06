#pragma once
#include <string>
#include <unistd.h> // for getuid

// Helper to get the default socket path based on user ID and XDG_RUNTIME_DIR
inline std::string get_default_audio_socket_path() {
    const char* xr = std::getenv("XDG_RUNTIME_DIR");
    if (xr && xr[0]) {
        return std::string(xr) + "/interactive-wallpaper-audio.sock";
    }
    // Fallback to /tmp if XDG_RUNTIME_DIR is not set
    uid_t uid = getuid();
    return std::string("/tmp/interactive-wallpaper-audio-") + std::to_string(uid) + ".sock";
}