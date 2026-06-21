// src/event-types.hpp
#pragma once
#include <variant>
#include "audio-data.hpp"

enum class EventType {
    PointerMotion,
    AudioData
    // В будущем тут легко добавить: SystemStats, KeyboardPress и т.д.
};

// Данные от мыши
struct PointerMotionPayload {
    double dx;
    double dy;
    bool is_touchpad;
};

// Используем std::monostate для событий без данных (если понадобятся)
using EventPayload = std::variant<std::monostate, AudioData, PointerMotionPayload>;

struct Event {
    EventType type;
    EventPayload payload;
};