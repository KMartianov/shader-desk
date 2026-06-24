// src/ipc-protocol.hpp
#pragma once
#include <cstdint>

// Мы больше не используем #pragma pack. 
// Структура выровнена вручную (кратно 4 байтам), что безопасно для любой архитектуры.
struct PointerDatagram {
    uint32_t magic;      // 4 байта
    float dx;            // 4 байта (double тут избыточен, float хватит за глаза)
    float dy;            // 4 байта
    uint8_t is_touchpad; // 1 байт
    uint8_t padding[3];  // 3 байта резерва для ровного размера структуры (16 байт)
};

// Для аудио пока оставим как есть, но выровняем позже
struct AudioDatagram {
    uint32_t magic;
    float volume;
    float bass, mid, treble;
    float bands[64];
};

const uint32_t AUDIO_MAGIC = 0x41554431;   // "AUD1"
const uint32_t POINTER_MAGIC = 0x50545231; // "PTR1"