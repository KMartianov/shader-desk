// Utilities/shader-desk-ctl.cpp
#include <iostream>
#include <string>
#include <vector>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <sys/time.h>
#include <cstring>
#include "ipc-utils.hpp"

/**
 * @brief Ensures the complete transmission of data over a stream socket.
 * 
 * POSIX 'write' on SOCK_STREAM does not guarantee sending all bytes in a single call.
 * This loop handles partial writes and safely retries if interrupted by a system signal (EINTR).
 */
bool send_full(int sockfd, const std::string& data) {
    size_t total_sent = 0;
    while (total_sent < data.size()) {
        ssize_t sent = write(sockfd, data.data() + total_sent, data.size() - total_sent);
        if (sent < 0) {
            if (errno == EINTR) continue; // Interrupted by a signal, safe to retry
            return false;                 // Actual socket error (e.g., EPIPE)
        }
        total_sent += sent;
    }
    return true;
}

int main(int argc, char* argv[]) {
    std::string command;

    // 1. Input Collection Phase
    if (argc > 1) {
        // Command provided via CLI arguments. Assemble them into a single string.
        for (int i = 1; i < argc; ++i) {
            command += argv[i];
            if (i < argc - 1) command += " ";
        }
    } else {
        // No CLI arguments. Determine if data is being piped into stdin.
        // isatty() returns true if stdin is connected to a terminal (meaning no pipe).
        if (!isatty(STDIN_FILENO)) {
            // Read the entirety of the piped stream (e.g., 'cat script.lua | shader-desk-ctl')
            std::istreambuf_iterator<char> begin(std::cin), end;
            command.assign(begin, end);
        } else {
            std::cerr << "Usage: shader-desk-ctl <lua_command>\n";
            std::cerr << "   or: cat script.lua | shader-desk-ctl\n";
            return 1;
        }
    }

    if (command.empty()) {
        return 0; // Nothing to do
    }

    // Null-Terminated Framing: 
    // Append the null-byte delimiter required by the core's stream parser.
    command.push_back('\0');

    // 2. IPC Connection Setup
    int sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sockfd < 0) {
        std::cerr << "[Error] Failed to create UNIX socket.\n";
        return 1;
    }

    // Timeouts: Prevent Window Manager (Wayland) freezes.
    // If the core engine deadlocks, this client will safely abort after 2 seconds
    // instead of hanging the user's hotkey execution pipeline.
    struct timeval tv{};
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
    setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::string socket_path = shader_desk::get_ipc_socket_path("shader-desk");
    strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);
    
    if (connect(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "[Error] Failed to connect to '" << socket_path 
                  << "'. Is the interactive-wallpaper engine running?\n";
        close(sockfd);
        return 1;
    }

    // 3. Command Transmission
    if (!send_full(sockfd, command)) {
        std::cerr << "[Error] Failed to transmit data to the engine (Timeout or Broken Pipe).\n";
        close(sockfd);
        return 1;
    }

    // 4. Response Aquisition
    // Reads the incoming stream until the null-terminator is encountered, ensuring 
    // large payloads (like JSON status objects) are fully assembled regardless of OS fragmentation.
    std::string response;
    char buffer[4096];
    bool done = false;

    while (!done) {
        ssize_t bytes_read = read(sockfd, buffer, sizeof(buffer));
        if (bytes_read < 0) {
            if (errno == EINTR) continue;
            std::cerr << "[Error] Timeout or connection lost while waiting for engine response.\n";
            close(sockfd);
            return 1;
        } else if (bytes_read == 0) {
            // EOF: The server closed the connection gracefully.
            break;
        }

        // Scan the received chunk for the frame delimiter ('\0')
        for (ssize_t i = 0; i < bytes_read; ++i) {
            if (buffer[i] == '\0') {
                response.append(buffer, i);
                done = true;
                break;
            }
        }
        
        if (!done) {
            response.append(buffer, bytes_read);
        }
    }

    close(sockfd);

    // 5. Output and Exit Code Evaluation
    // By providing standard exit codes, this tool can be safely integrated into bash conditionals.
    if (response.find("LUA_ERR:") == 0) {
        // Route script execution errors to stderr with red formatting
        std::cerr << "\033[31m" << response << "\033[0m\n";
        return 1; 
    }

    // Suppress the generic "OK" acknowledgement, but output any requested data (like JSON) to stdout.
    if (!response.empty() && response != "OK") {
        std::cout << response << "\n";
    }

    return 0; // Success
}