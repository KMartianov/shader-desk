// src/core-context.hpp
#pragma once
#include <string>
#include <unordered_map>
#include <functional>
#include <cstdint>
#include <vector>
#include <stdexcept>
#include <array>
#include <cstring>

// BlackBoard - центральная шина данных (Zero-Latency Data Router)
class BlackBoard {
public:
    // Фиксированный размер блока для каждого ключа, чтобы избежать реаллокации
    static constexpr size_t MAX_ELEMENTS_PER_KEY = 256;
    static constexpr size_t STRING_BUFFER_SIZE = 256;

    float* bind_float(const std::string& key) {
        return bind_float_array(key, 1);
    }

    float* bind_float_array(const std::string& key, size_t requested_size) {
        if (requested_size > MAX_ELEMENTS_PER_KEY) {
            throw std::runtime_error("BlackBoard: requested size exceeds maximum limit");
        }

        auto it = memory_.find(key);
        if (it == memory_.end()) {
            // Создаем блок памяти один раз и навсегда.
            it = memory_.emplace(key, std::vector<float>(MAX_ELEMENTS_PER_KEY, 0.0f)).first;
        }
        return it->second.data();
    }

    char* bind_string(const std::string& key) {
        auto it = string_memory_.find(key);
        if (it == string_memory_.end()) {
            it = string_memory_.emplace(key, std::array<char, STRING_BUFFER_SIZE>{0}).first;
        }
        return it->second.data();
    }

    void set_string(const std::string& key, const std::string& value) {
        char* buffer = bind_string(key);
        std::strncpy(buffer, value.c_str(), STRING_BUFFER_SIZE - 1);
        buffer[STRING_BUFFER_SIZE - 1] = '\0'; // Гарантированный null-терминатор
    }



    std::vector<std::string> get_all_keys() const {
        std::vector<std::string> keys;
        for (const auto& pair : memory_) {
            keys.push_back(pair.first);
        }
        return keys;
    }


private:
    std::unordered_map<std::string, std::vector<float>> memory_;
    std::unordered_map<std::string, std::array<char, STRING_BUFFER_SIZE>> string_memory_;
};

// Интерфейс, через который плагины общаются с Ядром
class ICoreContext {
public:
    virtual ~ICoreContext() = default;
    virtual BlackBoard& get_blackboard() = 0;
    virtual void register_epoll_fd(int fd, std::function<void(uint32_t events)> callback) = 0;
    virtual void unregister_epoll_fd(int fd) = 0;
};