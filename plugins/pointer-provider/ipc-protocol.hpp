// src/ipc-protocol.hpp
#pragma once
#include <cstdint>

// We no longer use #pragma pack. 
// The structure is manually aligned (multiple of 4 bytes), which is safe for any architecture.
struct PointerDatagram {
    uint32_t magic;      // 4 байта
    float dx;            // 4 bytes (double is excessive here, float is plenty)
    float dy;            // 4 байта
    uint8_t is_touchpad; // 1 байт
    uint8_t padding[3];  // 3 bytes reserved to pad structure to a clean 16 bytes
};

// For audio, leave as is for now, but align later
struct AudioDatagram {
    uint32_t magic;
    float volume;
    float bass, mid, treble;
    float bands[64];
};

const uint32_t AUDIO_MAGIC = 0x41554431;   // "AUD1"
const uint32_t POINTER_MAGIC = 0x50545231; // "PTR1"