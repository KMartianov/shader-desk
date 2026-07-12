#pragma once
#include <cstdint>

#pragma pack(push, 1) // ABI protection specifically for this plugin
struct AudioDatagram {
    uint32_t magic = 0x41554431; 
    float volume = 0.0f;
    float bass = 0.0f;
    float mid = 0.0f;
    float treble = 0.0f;
    float bands[64] = {0.0f}; 
};
#pragma pack(pop)

// Guarantee that the daemon and provider compile identically
static_assert(sizeof(AudioDatagram) == 276, "AudioDatagram alignment failed");