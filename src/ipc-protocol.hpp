// src/ipc-protocol.hpp
#pragma once
#include <cstdint>

// Отключаем выравнивание компилятора, чтобы структура точно совпадала байт-в-байт
#pragma pack(push, 1)

struct AudioDatagram {
    uint32_t magic; // Магическое число для проверки целостности
    float volume;
    float bass, mid, treble;
    float bands[64]; // Весь спектр частот
};

struct PointerDatagram {
    uint32_t magic;
    double dx, dy;
    bool is_touchpad;
};

#pragma pack(pop)

const uint32_t AUDIO_MAGIC = 0x41554431; // "AUD1"
const uint32_t POINTER_MAGIC = 0x50545231; // "PTR1"
