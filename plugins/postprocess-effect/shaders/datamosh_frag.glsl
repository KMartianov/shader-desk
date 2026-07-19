#version 300 es
precision highp float;

in vec2 v_uv;
out vec4 FragColor;

uniform sampler2D u_prev_layer;
uniform float time;
uniform vec2 resolution;

// Параметры проброшены из C++ (Lua -> BlackBoard -> GLSL)
uniform float intensity;
uniform float speed;
uniform float scale;
uniform int variant;

// Быстрый PRNG для генерации шума без текстур
float rand(vec2 co) {
    return fract(sin(dot(co.xy ,vec2(12.9898,78.233))) * 43758.5453);
}

void main() {
    vec2 uv = v_uv;
    
    // Дискретное время (t) обеспечивает дерганую анимацию (как частота кадров сломанного видео)
    float t = floor(time * speed * 10.0);
    
    // Коррекция соотношения сторон для квадратных макро-блоков
    vec2 aspect = vec2(resolution.x / resolution.y, 1.0);

    // ------------------------------------------------------------------
    // ВАРИАНТ 0: PIXEL SORT / MELT (Тающие пиксели / Гравитация)
    // ------------------------------------------------------------------
    if (variant == 0) {
        // Разбиваем экран на тонкие вертикальные столбцы
        float col = floor(uv.x * resolution.x / max(scale, 1.0));
        
        // Шум определяет, "тает" ли этот столбец в текущем кадре
        float melt_trigger = rand(vec2(col, t));
        
        if (melt_trigger < intensity) {
            // Длина подтека зависит от второго случайного числа
            float melt_amount = rand(vec2(col, t + 1.0)) * 0.5;
            
            // Сдвигаем UV вверх, чтобы пиксели "потянулись" вниз.
            // pow(uv.y) делает так, что верхние пиксели тают меньше, а нижние - сильнее.
            uv.y -= pow(uv.y, 1.5) * melt_amount;
        }
    } 
    // ------------------------------------------------------------------
    // ВАРИАНТ 1: BLOCKY DATAMOSH (Артефакты сжатия видео)
    // ------------------------------------------------------------------
    else if (variant == 1) {
        // Разбиваем экран на крупные макро-блоки (как в JPEG/H264)
        vec2 block = floor(uv * scale * aspect);
        float block_noise = rand(block + t);
        
        // Потеря P-кадра: блок съезжает в сторону (Motion Vector Corruption)
        if (block_noise < intensity) {
            vec2 motion_vector = vec2(
                rand(block + 1.2) - 0.5,
                rand(block + 2.3) - 0.5
            ) * 0.1; // Радиус смещения
            
            uv += motion_vector;
        }
        
        // Эффект потери цветности (Chroma Subsampling / Пикселизация блока)
        if (block_noise < intensity * 0.5) {
            // Искусственно занижаем разрешение чтения текстуры для этого блока
            float pixel_factor = scale * 2.0;
            uv = floor(uv * pixel_factor) / pixel_factor;
        }
    }
    // ------------------------------------------------------------------
    // ВАРИАНТ 2: VHS HORIZONTAL STRETCH (Залипание магнитной ленты)
    // ------------------------------------------------------------------
    else if (variant == 2) {
        // Очень широкие и тонкие блоки (имитация строк развертки)
        vec2 line_block = floor(uv * vec2(2.0, scale));
        float line_noise = rand(line_block + t);
        
        if (line_noise < intensity) {
            // Горизонтальный разрыв и растяжение
            float stretch = rand(line_block + 1.0) * 0.2;
            uv.x -= stretch;
        }
    }

    // Защита от выхода за пределы текстуры
    uv = clamp(uv, 0.0, 1.0);
    
    vec4 col = texture(u_prev_layer, uv);

    if (col.a < 0.01) {
        discard;
    }
    
    FragColor = col;
}