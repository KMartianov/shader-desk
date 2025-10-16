// plugins/simple-wave-effect/shaders/wave_frag.glsl
#version 300 es
precision highp float;

// Входные данные от вершинного шейдера (координаты текселя)
in vec2 v_uv;

// Выходной цвет
out vec4 FragColor;

// Стандартные uniform-переменные, всегда доступны
uniform float time;
uniform vec2 resolution;

// Пользовательские параметры, которые будут управляться из конфига.
// Формат: @param <имя_в_коде> | <тип> | <значение_по_умолчанию> | <описание>

// @param wave_color | vec3 | 0.1, 0.5, 1.0 | Основной цвет волн (R,G,B)
uniform vec3 wave_color;

// @param speed | float | 2.5 | Скорость движения волн
uniform float speed;

// @param frequency | float | 20.0 | Частота (количество) волн на экране
uniform float frequency;

// @param is_inverted | bool | false | Инвертировать ли цвета
uniform bool is_inverted;

void main() {
    // Нормализуем координаты, чтобы центр был (0,0)
    vec2 uv = v_uv - 0.5;
    
    // Простое уравнение волны, зависящее от времени и расстояния от центра
    float wave = sin(length(uv) * frequency - time * speed);
    
    // Сделаем волны более плавными
    wave = smoothstep(0.0, 1.0, wave);
    
    vec3 final_color = wave_color * wave;
    
    if (is_inverted) {
        final_color = vec3(1.0) - final_color;
    }
    
    FragColor = vec4(final_color, 1.0);
}