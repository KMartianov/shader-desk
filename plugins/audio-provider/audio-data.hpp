#pragma once
#include <cstdint>

// The structure is naturally 4-byte aligned, pack(1) is not needed.
struct AudioData {
    uint32_t magic = 0x41554431; // Magic number "AUD1" 
    float volume = 0.0f;
    float bass = 0.0f;
    float mid = 0.0f;
    float treble = 0.0f;
    float bands[64] = {0.0f}; 
};