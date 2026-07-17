#version 300 es
precision highp float;

in vec2 v_uv;
out vec4 FragColor;

uniform sampler2D u_prev_layer;
uniform vec2 resolution;
uniform float time;

// Параметры
uniform float intensity; // Степень искажения линзы и расслоения цветов
uniform float speed;     // Скорость мерцания / дрожания
uniform float scale;     // Плотность сканлайнов
uniform int variant;     // 0: Color, 1: Green Terminal, 2: Black & White

// Функция искривления экрана (Curvature)
vec2 curve_uv(vec2 uv, float warp) {
    uv = uv * 2.0 - 1.0; // Перевод в -1..1
    vec2 offset = abs(uv.yx) / vec2(warp, warp);
    uv = uv + uv * offset * offset;
    uv = uv * 0.5 + 0.5; // Возврат в 0..1
    return uv;
}

void main() {
    // 1. Искривление экрана (Lens Distortion)
    // Чем выше intensity, тем сильнее "пузатость" монитора. Базовое значение 4.0.
    float warp_factor = 4.0 - clamp(intensity, 0.0, 1.0) * 2.0; 
    vec2 uv = curve_uv(v_uv, warp_factor);

    // Защита: рисуем черную рамку за пределами "кинескопа"
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    // 2. Хроматическая аберрация по краям экрана
    float color_split = (length(uv - 0.5) * 0.02) * intensity;
    
    // Легкий V-Sync Jitter (дрожание картинки по вертикали)
    float jitter = sin(time * speed * 10.0) * 0.002 * intensity;
    vec2 uv_r = vec2(uv.x + color_split, uv.y + jitter);
    vec2 uv_g = vec2(uv.x,               uv.y);
    vec2 uv_b = vec2(uv.x - color_split, uv.y - jitter);

    vec3 col;
    col.r = texture(u_prev_layer, uv_r).r;
    col.g = texture(u_prev_layer, uv_g).g;
    col.b = texture(u_prev_layer, uv_b).b;

    // 3. Scanlines (Горизонтальные полосы развертки)
    // Scale управляет количеством строк на экране
    float scanline = sin(uv.y * resolution.y * (scale * 0.1)) * 0.04;
    col -= scanline;

    // 4. Виньетирование (Затемнение углов кинескопа)
    float vignette = length(uv - 0.5);
    col *= 1.0 - vignette * 0.5;

    // 5. Применение цветовых фильтров (Variant)
    if (variant == 1) {
        // Green Terminal: Оставляем только яркость в зеленом канале
        float lum = dot(col, vec3(0.299, 0.587, 0.114));
        col = vec3(0.05, lum * 1.2, 0.05);
    } else if (variant == 2) {
        // Black & White TV
        float lum = dot(col, vec3(0.299, 0.587, 0.114));
        col = vec3(lum);
    }

    // Добавляем легкое свечение экрана (Phosphor glow)
    col *= 1.2;

    FragColor = vec4(col, 1.0);
}