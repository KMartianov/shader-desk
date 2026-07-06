// shader-desk/src/audio-data.hpp
#pragma once
#include <cstdint>

// Отключаем выравнивание компилятора (чтобы структура передавалась байт-в-байт)
#pragma pack(push, 1)

struct AudioData {
    uint32_t magic = 0x41554431; // Магическое число "AUD1" для проверки целостности
    float volume = 0.0f;
    float bass = 0.0f;
    float mid = 0.0f;
    float treble = 0.0f;
    float bands[64] = {0.0f}; // Фиксированный массив на 64 полосы
};

#pragma pack(pop)
