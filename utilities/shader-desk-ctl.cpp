// utilities/shader-desk-ctl.cpp
#include <iostream>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: shader-desk-ctl <lua_command>\n";
        std::cerr << "Example: shader-desk-ctl \"core.set_effect_param('eDP-1', 'wireframe_mode', false)\"\n";
        return 1;
    }

    // Собираем аргументы в одну строку команды
    std::string command;
    for (int i = 1; i < argc; ++i) {
        command += argv[i];
        if (i < argc - 1) command += " ";
    }

    int sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sockfd < 0) {
        std::cerr << "Failed to create socket.\n";
        return 1;
    }

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, "/run/user/1000/shader-desk.sock", sizeof(addr.sun_path) - 1);

    if (connect(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "Failed to connect to shader-desk. Is the wallpaper engine running?\n";
        close(sockfd);
        return 1;
    }

    // Отправляем команду
    write(sockfd, command.c_str(), command.length());

    // Читаем ответ от сервера
    char buffer[1024]{0};
    ssize_t n = read(sockfd, buffer, sizeof(buffer) - 1);
    if (n > 0) {
        std::cout << buffer;
    }

    close(sockfd);
    return 0;
}