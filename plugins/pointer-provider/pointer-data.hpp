#pragma once
#include <cstdint>

constexpr uint32_t POINTER_MAGIC = 0x50545231;

#pragma pack(push, 1)
struct PointerDatagram {
    uint32_t magic = POINTER_MAGIC;
    float dx = 0.0f;
    float dy = 0.0f;
    uint8_t is_touchpad = 0;
    uint8_t padding[3] = {0, 0, 0};
};
#pragma pack(pop)

static_assert(sizeof(PointerDatagram) == 16, "PointerDatagram alignment failed");