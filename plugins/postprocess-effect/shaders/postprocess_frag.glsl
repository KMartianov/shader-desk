#version 300 es
precision highp float;

in vec2 v_uv;
out vec4 FragColor;

// --- Системные униформы ---
uniform sampler2D u_prev_layer; // Автоматически подхватит Ping-Pong FBO из ядра
uniform float time;
uniform vec2 resolution;

// ==============================================================================
// УНИВЕРСАЛЬНЫЕ ПАРАМЕТРЫ (Автогенерация C++ API)
// ==============================================================================

// @param intensity | float | 0.3 | Сила глитча и расслоения RGB
uniform float intensity;

// @param speed | float | 2.0 | Скорость смены артефактов
uniform float speed;

// @param scale | float | 15.0 | Плотность блоков глитча (чем больше, тем мельче)
uniform float scale;

// @param variant | int | 1 | 0: Smooth RGB, 1: Block VHS, 2: Hardcore Scanlines
uniform int variant;

// ==============================================================================
// МАТЕМАТИКА
// ==============================================================================

// Быстрый, дешевый генератор псевдослучайных чисел
float rand(vec2 co) {
    return fract(sin(dot(co.xy ,vec2(12.9898,78.233))) * 43758.5453);
}

void main() {
    vec2 uv = v_uv;
    float t = floor(time * speed * 10.0); // Дискретное время для "дерганой" анимации
    
    float split_dist = intensity * 0.03; // Базовый сдвиг цветов

    // --- ЛОГИКА ГЛИТЧА ---
    if (variant == 1 || variant == 2) {
        // Разбиваем экран на горизонтальные блоки
        float block = floor(uv.y * scale); 
        
        // Генерируем случайное число для каждого блока в текущем кадре
        float block_noise = rand(vec2(t, block)); 
        
        // Если шум превышает порог (зависит от intensity), сдвигаем блок
        if (block_noise > 1.0 - (intensity * 0.5)) {
            float shift = (rand(vec2(block, t + 1.0)) - 0.5) * intensity * 0.1;
            uv.x += shift;
            split_dist *= 2.0; // Усиливаем RGB сплит в сломанных блоках
        }
        
        // Вариант 2: Добавляем жесткие Scanlines (ЭЛТ монитор)
        if (variant == 2) {
            float scanline = sin(uv.y * resolution.y * 1.5) * 0.04 * intensity;
            uv.x += scanline * block_noise;
        }
    }

    // --- ХРОМАТИЧЕСКАЯ АБЕРРАЦИЯ (RGB Split) ---
    // Трижды читаем FBO с немного смещенными координатами
    vec2 uv_r = uv + vec2(split_dist, 0.0);
    vec2 uv_b = uv - vec2(split_dist, 0.0);
    
    // Защита от выхода за пределы текстуры (wrap)
    uv_r = clamp(uv_r, 0.0, 1.0);
    uv_b = clamp(uv_b, 0.0, 1.0);
    uv   = clamp(uv,   0.0, 1.0);

    float r = texture(u_prev_layer, uv_r).r;
    float g = texture(u_prev_layer, uv).g;
    float b = texture(u_prev_layer, uv_b).b;

    FragColor = vec4(r, g, b, 1.0);
}