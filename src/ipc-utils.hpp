#pragma once
#include <string>
#include <cstdlib>
#include <unistd.h>
#include <sys/un.h> // Required for sockaddr_un

namespace shader_desk {

// Helps plugin authors create safe sockets in XDG_RUNTIME_DIR
// Includes protection against path buffer overflows (max 108 bytes).
inline std::string get_ipc_socket_path(const std::string& app_name) {
    std::string fallback_path = "/tmp/" + app_name + "-" + std::to_string(getuid()) + ".sock";
    const char* xdg_runtime = std::getenv("XDG_RUNTIME_DIR");
    
    if (xdg_runtime && xdg_runtime[0] != '\0') {
        std::string preferred_path = std::string(xdg_runtime) + "/" + app_name + ".sock";
        
        // SAFETY: sun_path has a strict limit of 108 bytes. 
        // If XDG_RUNTIME_DIR is heavily nested (e.g., Flatpak/Snap environments), fallback to /tmp.
        if (preferred_path.length() >= sizeof(sockaddr_un::sun_path)) {
            return fallback_path;
        }
        return preferred_path;
    }
    
    return fallback_path;
}

} // Namespace shader_desk