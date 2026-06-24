#pragma once
#include <cstdint>

// Структура естественно выровнена по 4 байта, pack(1) не нужен.
struct AudioData {
    uint32_t magic = 0x41554431; // Магическое число "AUD1" 
    float volume = 0.0f;
    float bass = 0.0f;
    float mid = 0.0f;
    float treble = 0.0f;
    float bands[64] = {0.0f}; 
};