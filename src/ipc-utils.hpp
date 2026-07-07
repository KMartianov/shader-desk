#pragma once
#include <string>
#include <cstdlib>
#include <unistd.h>

namespace shader_desk {

// Помогает авторам плагинов создавать безопасные сокеты в XDG_RUNTIME_DIR
inline std::string get_ipc_socket_path(const std::string& app_name) {
    const char* xdg_runtime = std::getenv("XDG_RUNTIME_DIR");
    if (xdg_runtime && xdg_runtime[0] != '\0') {
        return std::string(xdg_runtime) + "/" + app_name + ".sock";
    }
    return "/tmp/" + app_name + "-" + std::to_string(getuid()) + ".sock";
}

} // namespace shader_desk