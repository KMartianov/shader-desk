#version 300 es
precision highp float;

in vec2 v_uv;
out vec4 FragColor;

uniform sampler2D u_prev_layer;
uniform vec2 resolution;
uniform float time;

// Параметры
uniform float intensity; // Зум (отдаление/приближение узора)
uniform float speed;     // Скорость непрерывного вращения
uniform float scale;     // Количество граней зеркала (сегментов)
uniform int variant;     // 0: Обычный, 1: Инвертированное время (вращение в обратную сторону)

#define PI 3.14159265359

void main() {
    // 1. Коррекция соотношения сторон, чтобы зеркала были круглыми, а не овальными
    vec2 aspect = vec2(resolution.x / resolution.y, 1.0);
    vec2 uv = (v_uv - 0.5) * aspect;

    // 2. Перевод в полярные координаты
    float radius = length(uv);
    float angle = atan(uv.y, uv.x);

    // 3. Зум (intensity)
    radius *= (1.0 - clamp(intensity, 0.0, 0.9));

    // 4. Вращение всей конструкции
    float rot_speed = speed * 0.5;
    if (variant == 1) rot_speed = -rot_speed;
    angle += time * rot_speed;

    // 5. Логика зеркал Калейдоскопа
    // scale определяет количество сегментов (например, 6)
    float segments = max(3.0, floor(scale)); 
    float segment_angle = (PI * 2.0) / segments;
    
    // Заворачиваем угол в один сегмент
    angle = mod(angle, segment_angle);
    
    // Зеркальное отражение внутри сегмента
    angle = abs(angle - segment_angle / 2.0);

    // 6. Обратный перевод в декартовы (обычные) координаты
    vec2 k_uv = vec2(cos(angle), sin(angle)) * radius;
    
    // Возвращаем aspect ratio и сдвигаем обратно в 0..1
    k_uv = (k_uv / aspect) + 0.5;

    // Отражение координат, если они вышли за пределы 0..1 (бесконечный тайлинг зеркала)
    k_uv = abs(mod(k_uv, 2.0) - 1.0);

    FragColor = texture(u_prev_layer, k_uv);
}