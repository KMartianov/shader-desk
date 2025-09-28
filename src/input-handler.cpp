#include "input-handler.hpp"
#include <iostream>
#include <wayland-client.h>

// Listener definitions
const struct wl_seat_listener InputHandler::seat_listener = {
    .capabilities = InputHandler::seat_capabilities,
    .name = InputHandler::seat_name,
};

const struct wl_pointer_listener InputHandler::pointer_listener = {
    .enter = InputHandler::pointer_enter,
    .leave = InputHandler::pointer_leave,
    .motion = InputHandler::pointer_motion,
    .button = InputHandler::pointer_button,
    .axis = InputHandler::pointer_axis,
    .frame = InputHandler::pointer_frame,
    .axis_source = InputHandler::pointer_axis_source,
    .axis_stop = InputHandler::pointer_axis_stop,
    .axis_discrete = InputHandler::pointer_axis_discrete,
};


InputHandler::InputHandler() {}

InputHandler::~InputHandler() {
    if (pointer) {
        wl_pointer_release(pointer);
        pointer = nullptr;
    }
    if (seat) {
        wl_seat_release(seat);
        seat = nullptr;
    }
}

bool InputHandler::initialize(wl_seat* seat) {
    this->seat = seat;
    wl_seat_add_listener(seat, &seat_listener, this);
    return true;
}

void InputHandler::setCallbacks(PointerMoveCallback move_cb, 
                               PointerClickCallback click_cb, 
                               PointerMotionCallback motion_cb) {
    on_pointer_move = move_cb;
    on_pointer_click = click_cb;
    on_pointer_motion = motion_cb;
}

// Input event handlers
void InputHandler::seat_capabilities(void* data, wl_seat* seat, uint32_t capabilities) {
    InputHandler* self = static_cast<InputHandler*>(data);
    
    if (capabilities & WL_SEAT_CAPABILITY_POINTER) {
        if (!self->pointer) {
            self->pointer = wl_seat_get_pointer(seat);
            wl_pointer_add_listener(self->pointer, &pointer_listener, self);
            std::cout << "Pointer capability acquired" << std::endl;
        }
    } else {
        if (self->pointer) {
            wl_pointer_release(self->pointer);
            self->pointer = nullptr;
            std::cout << "Pointer capability lost" << std::endl;
        }
    }
}

void InputHandler::seat_name(void* data, wl_seat* seat, const char* name) {
    std::cout << "Seat name: " << (name ? name : "unknown") << std::endl;
}

// input-handler.cpp (фрагмент)
#include "input-handler.hpp"
#include <iostream>
#include <limits> // для UINT32_MAX
#include <cmath>

void InputHandler::pointer_enter(void* data, wl_pointer* /*pointer*/, uint32_t /*serial*/,
                                 wl_surface* /*surface*/, wl_fixed_t sx, wl_fixed_t sy) {
    InputHandler* self = static_cast<InputHandler*>(data);
    self->pointer_x = wl_fixed_to_double(sx);
    self->pointer_y = wl_fixed_to_double(sy);
    self->last_pointer_x = self->pointer_x;
    self->last_pointer_y = self->pointer_y;
    // при заходе сбросим метку времени (чтобы следующее motion корректно инициализировало dt)
    self->last_motion_time = 0;
    if (self->on_pointer_move) self->on_pointer_move(self->pointer_x, self->pointer_y);
}

void InputHandler::pointer_motion(void* data, wl_pointer* /*pointer*/, uint32_t time,
                                  wl_fixed_t sx, wl_fixed_t sy) {
    InputHandler* self = static_cast<InputHandler*>(data);

    // предыдущее положение
    self->last_pointer_x = self->pointer_x;
    self->last_pointer_y = self->pointer_y;

    // обновим текущие координаты
    self->pointer_x = wl_fixed_to_double(sx);
    self->pointer_y = wl_fixed_to_double(sy);

    // при расчёте dt используем unsigned вычитание — оно корректно работает при wrap-around
    uint32_t dt_ms = (self->last_motion_time == 0) ? 0 : (time - self->last_motion_time);
    double dt = (dt_ms == 0) ? 0.0 : (static_cast<double>(dt_ms) / 1000.0);
    self->last_motion_time = time;

    double dx = self->pointer_x - self->last_pointer_x;
    double dy = self->pointer_y - self->last_pointer_y;

    double vx = 0.0, vy = 0.0;
    if (dt > 1e-6) { // защититься от деления на ноль / очень малого dt
        vx = dx / dt;
        vy = dy / dt;
    }

    // экспоненциальное сглаживание скоростей
    double alpha = self->smoothing_alpha;
    self->velocity_x = alpha * vx + (1.0 - alpha) * self->velocity_x;
    self->velocity_y = alpha * vy + (1.0 - alpha) * self->velocity_y;

    // Логирование (по желанию)
    std::cout << "Pointer motion - Pos: (" << self->pointer_x << ", " << self->pointer_y 
              << "), d: (" << dx << ", " << dy << "), v_raw: (" << vx << ", " << vy 
              << "), v_smooth: (" << self->velocity_x << ", " << self->velocity_y 
              << "), dt: " << dt << "s" << std::endl;

    // колбэк: dx, dy, vx_smoothed, vy_smoothed, dt
    if (self->on_pointer_motion) {
        self->on_pointer_motion(dx, dy, self->velocity_x, self->velocity_y, dt);
    }

    // также можно вызывать on_pointer_move с абсолютной позицией, если нужно
    if (self->on_pointer_move) {
        self->on_pointer_move(self->pointer_x, self->pointer_y);
    }
}


void InputHandler::pointer_leave(void* data, wl_pointer* pointer, uint32_t serial,
                                wl_surface* surface) {
    std::cout << "Pointer left" << std::endl;
}



void InputHandler::pointer_button(void* data, wl_pointer* pointer, uint32_t serial,
                                 uint32_t time, uint32_t button, uint32_t state) {
    InputHandler* self = static_cast<InputHandler*>(data);
    
    const char* button_name = "unknown";
    switch (button) {
        case BTN_LEFT: button_name = "left"; break;
        case BTN_RIGHT: button_name = "right"; break;
        case BTN_MIDDLE: button_name = "middle"; break;
    }
    
    const char* state_name = (state == WL_POINTER_BUTTON_STATE_PRESSED) ? "pressed" : "released";
    
    std::cout << "Pointer button: " << button_name << " button " << state_name 
              << " at (" << self->pointer_x << ", " << self->pointer_y << ")" << std::endl;
    
    // Only call callback for press events
    if (state == WL_POINTER_BUTTON_STATE_PRESSED && self->on_pointer_click) {
        self->on_pointer_click(self->pointer_x, self->pointer_y, button);
    }
}

void InputHandler::pointer_axis(void* data, wl_pointer* pointer, uint32_t time,
                               uint32_t axis, wl_fixed_t value) {
    InputHandler* self = static_cast<InputHandler*>(data);
    
    const char* axis_name = (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) ? "vertical" : "horizontal";
    std::cout << "Pointer axis: " << axis_name << " scroll with value " << wl_fixed_to_double(value) << std::endl;
}

void InputHandler::pointer_frame(void* /*data*/, wl_pointer* /*wl_pointer*/) {
    // Группировка событий: обычно не требуется действовать,
    // но полезно иметь обработчик для совместимости.
    // std::cout << "Pointer frame\n";
}

void InputHandler::pointer_axis_source(void* data, wl_pointer* /*wl_pointer*/, uint32_t axis_source) {
    InputHandler* self = static_cast<InputHandler*>(data);
    // можно сохранить источник для логики прокрутки, сейчас просто лог:
    std::cout << "Pointer axis source: " << axis_source << std::endl;
    (void)self; // если не используете поле — убрать предупреждение
}

void InputHandler::pointer_axis_stop(void* /*data*/, wl_pointer* /*wl_pointer*/, uint32_t time, uint32_t axis) {
    std::cout << "Pointer axis stop: axis=" << axis << " time=" << time << std::endl;
}

void InputHandler::pointer_axis_discrete(void* /*data*/, wl_pointer* /*wl_pointer*/, uint32_t axis, int32_t discrete) {
    std::cout << "Pointer axis discrete: axis=" << axis << " steps=" << discrete << std::endl;
}
