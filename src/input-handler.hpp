#pragma once

#include <wayland-client.h>
#include <linux/input.h>
#include <functional>
#include <cstdint>

class InputHandler {
public:
    struct PointerEvent {
        double x;
        double y;
        double dx;
        double dy;
        uint32_t button;
        uint32_t state; // WL_POINTER_BUTTON_STATE_PRESSED / _RELEASED
        uint32_t time;  // время события в миллисекундах
    };

    using PointerMoveCallback = std::function<void(double x, double y)>;
    using PointerClickCallback = std::function<void(double x, double y, uint32_t button)>;
    using PointerMotionCallback = std::function<void(double dx, double dy, double vx, double vy, double dt)>;

    InputHandler();
    ~InputHandler();

    // Инициализировать с wl_seat (добавляет слушатель)
    bool initialize(wl_seat* seat);

    // Установить колбэки
    void setCallbacks(PointerMoveCallback move_cb,
                      PointerClickCallback click_cb,
                      PointerMotionCallback motion_cb);

    // Wayland listener-структуры (определяются в .cpp)
    static const struct wl_seat_listener seat_listener;
    static const struct wl_pointer_listener pointer_listener;

    // Обработчики событий Wayland (статические, вызываются библиотекой)
    static void seat_capabilities(void* data, wl_seat* seat, uint32_t capabilities);
    static void seat_name(void* data, wl_seat* seat, const char* name);

    static void pointer_enter(void* data, wl_pointer* pointer, uint32_t serial,
                              wl_surface* surface, wl_fixed_t sx, wl_fixed_t sy);
    static void pointer_leave(void* data, wl_pointer* pointer, uint32_t serial,
                              wl_surface* surface);
    static void pointer_motion(void* data, wl_pointer* pointer, uint32_t time,
                               wl_fixed_t sx, wl_fixed_t sy);
    static void pointer_button(void* data, wl_pointer* pointer, uint32_t serial,
                               uint32_t time, uint32_t button, uint32_t state);
    static void pointer_axis(void* data, wl_pointer* pointer, uint32_t time,
                             uint32_t axis, wl_fixed_t value);

    // новые обработчики, соответствуют сигнатурам в wayland-client-protocol.h
    static void pointer_frame(void* data, wl_pointer* wl_pointer);
    static void pointer_axis_source(void* data, wl_pointer* wl_pointer, uint32_t axis_source);
    static void pointer_axis_stop(void* data, wl_pointer* wl_pointer, uint32_t time, uint32_t axis);
    static void pointer_axis_discrete(void* data, wl_pointer* wl_pointer, uint32_t axis, int32_t discrete);


private:
    // Wayland объекты
    wl_pointer* pointer = nullptr;
    wl_seat* seat = nullptr;

    // Координаты и время
    double pointer_x = 0.0;
    double pointer_y = 0.0;
    double last_pointer_x = 0.0;
    double last_pointer_y = 0.0;
    uint32_t last_motion_time = 0; // 0 = неинициализировано

    // Сглаженные скорости
    double velocity_x = 0.0;
    double velocity_y = 0.0;
    double smoothing_alpha = 0.25; // 0..1 (1.0 = без сглаживания)

    // Колбэки пользователя
    PointerMoveCallback on_pointer_move = nullptr;
    PointerClickCallback on_pointer_click = nullptr;
    PointerMotionCallback on_pointer_motion = nullptr;
};
