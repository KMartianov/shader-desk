#pragma once
#include <cstdint>

constexpr uint32_t POINTER_MAGIC = 0x50545231;
#pragma pack(push, 1)
struct PointerDatagram {
    uint32_t magic = POINTER_MAGIC;
    float abs_x = 0.0f;  // От 0.0 до 1.0
    float abs_y = 0.0f;
    float rel_dx = 0.0f; // Физические дельты мыши
    float rel_dy = 0.0f;
    uint8_t is_absolute = 0; // 1 = тачскрин/планшет, 0 = мышь
    uint8_t padding[3] = {0};
};
#pragma pack(pop)
static_assert(sizeof(PointerDatagram) == 24, "Alignment failed");