// Src/core-context.hpp
#pragma once

#include "plugin-abi.hpp" 
#include <string>
#include <unordered_map>
#include <vector>
#include <array>
#include <cstring>
#include <stdexcept>
#include <iostream>

// BlackBoard - Central data bus (Zero-Latency Data Router)
// Inherits a safe ABI interface to hide STL internals from dynamically loaded plugins.
class BlackBoard : public IBlackBoardABI {
public:
    static constexpr size_t MAX_ELEMENTS_PER_KEY = 256;
    static constexpr size_t STRING_BUFFER_SIZE = 256;

    // ==============================================================================
    // C-ABI METHOD IMPLEMENTATIONS FOR PLUGINS
    // ==============================================================================
    
    float* bind_float(const char* key) override {
        return bind_float_array(key, 1);
    }

    float* bind_float_array(const char* key, size_t requested_size) override {
        // Protect the core from nullptr dereferencing by external plugins
        if (!key) return get_trash_float_buffer(); 

        // Enforce strict memory limits to prevent out-of-bounds ABI breaches
        if (requested_size > MAX_ELEMENTS_PER_KEY) {
            std::cerr << "\033[33m[BlackBoard] Warning: Array size exceeds maximum limit for key: " << key << "\033[0m\n";
            return get_trash_float_buffer();
        }
        
        std::string str_key(key);
        auto it = memory_.find(str_key);
        if (it == memory_.end()) {
            it = memory_.emplace(str_key, std::vector<float>(MAX_ELEMENTS_PER_KEY, 0.0f)).first;
        }
        return it->second.data();
    }

    char* bind_string(const char* key) override {
        if (!key) return get_trash_string_buffer();
        
        std::string str_key(key);
        auto it = string_memory_.find(str_key);
        if (it == string_memory_.end()) {
            it = string_memory_.emplace(str_key, std::array<char, STRING_BUFFER_SIZE>{0}).first;
        }
        return it->second.data();
    }

    void* bind_raw(const char* key, size_t requested_size_bytes) override {
        if (!key) return get_trash_raw_buffer();
        
        std::string str_key(key);
        auto it = raw_memory_.find(str_key);
        if (it == raw_memory_.end()) {
            it = raw_memory_.emplace(str_key, std::vector<uint8_t>(requested_size_bytes, 0)).first;
        } else if (it->second.size() < requested_size_bytes) {
            // Memory resizing is strictly prohibited after initialization to prevent dangling pointers
            std::cerr << "\033[31m[BlackBoard] Error: Cannot resize existing raw buffer: " << str_key << "\033[0m\n";
            return get_trash_raw_buffer();
        }
        return it->second.data();
    }

    void set_string(const char* key, const char* value) override {
        if (!key) return;
        if (!value) value = ""; // Protect std::strncpy from null pointers
        
        char* buffer = bind_string(key);
        
        // Prevent writing to the fallback trash memory
        if (buffer == get_trash_string_buffer()) return; 
        
        std::strncpy(buffer, value, STRING_BUFFER_SIZE - 1);
        buffer[STRING_BUFFER_SIZE - 1] = '\0'; // Guarantee strict null-termination
    }

    // ==============================================================================
    // INTERNAL CORE METHODS
    // These methods are safely hidden from the ABI and are only used by the microkernel.
    // ==============================================================================
    
    std::vector<std::string> get_all_keys() const {
        std::vector<std::string> keys;
        keys.reserve(memory_.size());
        for (const auto& pair : memory_) {
            keys.push_back(pair.first);
        }
        return keys;
    }

private:
    std::unordered_map<std::string, std::vector<float>> memory_;
    std::unordered_map<std::string, std::array<char, STRING_BUFFER_SIZE>> string_memory_;
    std::unordered_map<std::string, std::vector<uint8_t>> raw_memory_;

    // ==============================================================================
    // ZERO-ALLOCATION FALLBACK BUFFERS (Trash Buffers)
    // By utilizing static std::array, memory is allocated entirely in the .bss/.data 
    // segment during binary load time. This guarantees that fallback triggers will 
    // never invoke malloc() or lock heap mutexes during the critical render loop.
    // ==============================================================================
    
    float* get_trash_float_buffer() {
        static std::array<float, MAX_ELEMENTS_PER_KEY> trash{0.0f};
        return trash.data();
    }

    char* get_trash_string_buffer() {
        static std::array<char, STRING_BUFFER_SIZE> trash{0};
        return trash.data();
    }

    void* get_trash_raw_buffer() {
        static std::array<uint8_t, 1024> trash{0}; // 1KB of safe padding
        return trash.data();
    }
};