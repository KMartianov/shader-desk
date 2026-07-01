// src/core-context.hpp
#pragma once

#include "plugin-abi.hpp" // Подключаем интерфейсы ABI
#include <string>
#include <unordered_map>
#include <vector>
#include <array>
#include <cstring>
#include <stdexcept>

// BlackBoard - центральная шина данных (Zero-Latency Data Router)
// Наследует безопасный ABI интерфейс, чтобы плагины не видели STL-кишки.
class BlackBoard : public IBlackBoardABI {
public:
    static constexpr size_t MAX_ELEMENTS_PER_KEY = 256;
    static constexpr size_t STRING_BUFFER_SIZE = 256;

    // --- 1. РЕАЛИЗАЦИЯ C-ABI МЕТОДОВ ДЛЯ ПЛАГИНОВ ---
    
    float* bind_float(const char* key) override {
        return bind_float_array(key, 1);
    }

    float* bind_float_array(const char* key, size_t requested_size) override {
        if (requested_size > MAX_ELEMENTS_PER_KEY) {
            throw std::runtime_error("BlackBoard: requested size exceeds maximum limit");
        }
        std::string str_key(key);
        auto it = memory_.find(str_key);
        if (it == memory_.end()) {
            it = memory_.emplace(str_key, std::vector<float>(MAX_ELEMENTS_PER_KEY, 0.0f)).first;
        }
        return it->second.data();
    }

    char* bind_string(const char* key) override {
        std::string str_key(key);
        auto it = string_memory_.find(str_key);
        if (it == string_memory_.end()) {
            it = string_memory_.emplace(str_key, std::array<char, STRING_BUFFER_SIZE>{0}).first;
        }
        return it->second.data();
    }

    void set_string(const char* key, const char* value) override {
        char* buffer = bind_string(key);
        std::strncpy(buffer, value, STRING_BUFFER_SIZE - 1);
        buffer[STRING_BUFFER_SIZE - 1] = '\0'; // Гарантированный null-терминатор
    }

    // --- 2. ВНУТРЕННИЕ МЕТОДЫ ЯДРА (Недоступны через IBlackBoardABI) ---
    
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