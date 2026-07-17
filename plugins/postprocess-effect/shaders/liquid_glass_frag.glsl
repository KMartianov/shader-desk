#version 300 es
precision highp float;

in vec2 v_uv;
out vec4 FragColor;

uniform sampler2D u_prev_layer;
uniform vec2 resolution;
uniform float time;

// Параметры
uniform float intensity; // Сила искажения (преломления)
uniform float speed;     // Скорость течения жидкости
uniform float scale;     // Размер капель/волн на стекле
uniform int variant;     // 0: Текущая вода (Rain), 1: Рельефное стекло (Frosted)

// Дешевый генератор случайных чисел для Value Noise
float hash(vec2 p) {
    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

// 2D Value Noise (гладкий шум)
float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f); // Smoothstep интерполяция

    return mix(
        mix(hash(i + vec2(0.0, 0.0)), hash(i + vec2(1.0, 0.0)), u.x),
        mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), u.x),
        u.y
    );
}

// Функция для вычисления градиента (нормали) шума. 
// Именно она дает нам направление преломления света!
vec2 noise_gradient(vec2 p) {
    float eps = 0.01; // Шаг для вычисления производной
    float nX = noise(p + vec2(eps, 0.0)) - noise(p - vec2(eps, 0.0));
    float nY = noise(p + vec2(0.0, eps)) - noise(p - vec2(0.0, eps));
    return vec2(nX, nY) / (2.0 * eps);
}

void main() {
    vec2 aspect = vec2(resolution.x / resolution.y, 1.0);
    vec2 uv = v_uv;
    
    // Координаты для генерации шума
    vec2 noise_uv = uv * aspect * scale;
    
    // Анимация
    if (variant == 0) {
        // Режим текущей воды: координаты едут вниз (y - time)
        noise_uv.y -= time * speed;
        // Добавляем легкое диагональное смещение для реалистичности
        noise_uv.x += sin(time * speed * 0.5) * 0.5;
    } else {
        // Режим рельефного стекла: стекло стоит на месте, 
        // но слегка "плавится" (движется третья координата, симулируемая временем)
        noise_uv += noise(noise_uv + time * speed) * 0.5;
    }

    // Вычисляем вектор преломления на основе производной рельефа (Bump mapping)
    vec2 refraction = noise_gradient(noise_uv);
    
    // Сдвигаем оригинальные UV координаты на вектор преломления
    // Чем больше intensity, тем толще "стекло"
    vec2 distorted_uv = uv + refraction * (intensity * 0.05);
    
    // Clamp для защиты от артефактов на краях экрана
    distorted_uv = clamp(distorted_uv, 0.0, 1.0);

    // Сэмплим нижний слой с искажением
    vec3 col = texture(u_prev_layer, distorted_uv).rgb;

    // Добавляем оптические блики (Specular Highlights) на верхушки волн/капель
    float highlight = max(0.0, dot(normalize(refraction), vec2(-0.5, 0.5)));
    highlight = pow(highlight, 4.0) * intensity * 0.5;
    col += vec3(highlight);

    FragColor = vec4(col, 1.0);
}