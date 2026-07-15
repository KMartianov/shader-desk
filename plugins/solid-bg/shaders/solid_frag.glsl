#version 300 es
// Максимальная точность критически важна для градиентов без артефактов
precision highp float;

in vec2 v_uv;
out vec4 FragColor;

uniform vec2 resolution;

// ==============================================================================
// ПАРАМЕТРЫ ПЛАГИНА (АВТОГЕНЕРАЦИЯ C++)
// ==============================================================================

// @param gradient_type | int | 3 | 0: Solid, 1: Linear, 2: Radial, 3: 4-Corner Mesh
uniform int gradient_type;

// @param color_space | int | 1 | 0: sRGB (Standard), 1: Oklab (Perceptual smooth blending)
uniform int color_space;

// @param color_1 | vec3 | 0.89, 0.28, 0.20 | Primary Color (Top-Left)
uniform vec3 color_1;
// @param color_2 | vec3 | 0.15, 0.40, 0.85 | Secondary Color (Bottom-Right)
uniform vec3 color_2;
// @param color_3 | vec3 | 0.95, 0.75, 0.20 | Tertiary Color (Top-Right / Mesh only)
uniform vec3 color_3;
// @param color_4 | vec3 | 0.60, 0.20, 0.80 | Quaternary Color (Bottom-Left / Mesh only)
uniform vec3 color_4;

// @param angle | float | 45.0 | Angle in degrees (Linear only)
uniform float angle;

// @param radial_center | vec2 | 0.5, 0.5 | Center position (Radial only)
uniform vec2 radial_center;
// @param radial_radius | float | 1.2 | Gradient spread radius (Radial only)
uniform float radial_radius;

// @param dither_strength | float | 1.0 | TPDF Dithering intensity (1.0 = smooth 8-bit)
uniform float dither_strength;


// ==============================================================================
// БИБЛИОТЕКА: OKLAB COLOR SPACE (Björn Ottosson, 2020)
// ==============================================================================

// Перевод из sRGB в линейный RGB
vec3 srgb_to_linear(vec3 c) {
    vec3 linear1 = c / 12.92;
    vec3 linear2 = pow((c + 0.055) / 1.055, vec3(2.4));
    return mix(linear1, linear2, step(0.04045, c));
}

// Перевод из линейного RGB в sRGB
vec3 linear_to_srgb(vec3 c) {
    vec3 s1 = c * 12.92;
    vec3 s2 = 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055;
    return mix(s1, s2, step(0.0031308, c));
}

vec3 linear_srgb_to_oklab(vec3 c) {
    float l = 0.4122214708f * c.r + 0.5363325363f * c.g + 0.0514459929f * c.b;
    float m = 0.2119034982f * c.r + 0.6806995451f * c.g + 0.1073969566f * c.b;
    float s = 0.0883024619f * c.r + 0.2817188376f * c.g + 0.6299787005f * c.b;

    float l_ = sign(l) * pow(abs(l), 1.0/3.0);
    float m_ = sign(m) * pow(abs(m), 1.0/3.0);
    float s_ = sign(s) * pow(abs(s), 1.0/3.0);

    return vec3(
        0.2104542553f*l_ + 0.7936177850f*m_ - 0.0040720468f*s_,
        1.9779984951f*l_ - 2.4285922050f*m_ + 0.4505937099f*s_,
        0.0259040371f*l_ + 0.7827717662f*m_ - 0.8086757660f*s_
    );
}

vec3 oklab_to_linear_srgb(vec3 c) {
    float l_ = c.x + 0.3963377774f * c.y + 0.2158037573f * c.z;
    float m_ = c.x - 0.1055613458f * c.y - 0.0638541728f * c.z;
    float s_ = c.x - 0.0894841775f * c.y - 1.2914855480f * c.z;

    float l = l_*l_*l_;
    float m = m_*m_*m_;
    float s = s_*s_*s_;

    return vec3(
        + 4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s,
        - 1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s,
        - 0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s
    );
}

// ==============================================================================
// БИБЛИОТЕКА: DITHERING
// ==============================================================================

// Быстрый ГПСЧ
float rand(vec2 co) {
    return fract(sin(dot(co, vec2(12.9898, 78.233))) * 43758.5453);
}

// Triangular Probability Density Function (TPDF) Dithering
// Складываем два независимых шума для получения треугольного распределения.
// Это лучший алгоритм: он убирает бэндинг, но не создает эффект "грязной пленки".
float tpdf_dither(vec2 pos) {
    float n1 = rand(pos);
    float n2 = rand(pos + vec2(13.37, 42.01));
    // Нормализуем в диапазон [-1/255, 1/255] для 8-битного монитора
    return (n1 + n2 - 1.0) / 255.0; 
}


// ==============================================================================
// MAIN LOGIC
// ==============================================================================

void main() {
    // 1. Подготовка цветов (конвертация в Oklab, если требуется)
    vec3 c1 = (color_space == 1) ? linear_srgb_to_oklab(srgb_to_linear(color_1)) : color_1;
    vec3 c2 = (color_space == 1) ? linear_srgb_to_oklab(srgb_to_linear(color_2)) : color_2;
    vec3 c3 = (color_space == 1) ? linear_srgb_to_oklab(srgb_to_linear(color_3)) : color_3;
    vec3 c4 = (color_space == 1) ? linear_srgb_to_oklab(srgb_to_linear(color_4)) : color_4;

    vec3 mixed_color;

    // 2. Расчет градиента
    if (gradient_type == 0) {
        // Solid
        mixed_color = c1;
    } 
    else if (gradient_type == 1) {
        // Linear Angle
        float rad = radians(angle);
        vec2 dir = vec2(cos(rad), sin(rad));
        
        // Проекция UV на вектор направления
        float t = dot(v_uv - 0.5, dir) + 0.5;
        mixed_color = mix(c1, c2, clamp(t, 0.0, 1.0));
    } 
    else if (gradient_type == 2) {
        // Radial
        float aspect = resolution.x / resolution.y;
        vec2 uv = v_uv;
        vec2 center = radial_center;
        
        // Коррекция Aspect Ratio, чтобы круг не сжимался в овал
        uv.x = (uv.x - 0.5) * aspect + 0.5;
        center.x = (center.x - 0.5) * aspect + 0.5;
        
        float dist = distance(uv, center);
        float t = smoothstep(0.0, max(0.001, radial_radius), dist);
        
        mixed_color = mix(c1, c2, clamp(t, 0.0, 1.0));
    } 
    else {
        // 4-Corner Mesh
        vec3 top = mix(c1, c3, v_uv.x);
        vec3 bottom = mix(c4, c2, v_uv.x);
        mixed_color = mix(top, bottom, v_uv.y);
    }

    // 3. Возврат из Oklab в обычный sRGB для вывода на экран
    if (color_space == 1) {
        mixed_color = linear_to_srgb(oklab_to_linear_srgb(mixed_color));
    }

    // 4. Применение TPDF Dithering для устранения Color Banding
    if (dither_strength > 0.0) {
        mixed_color += tpdf_dither(gl_FragCoord.xy) * dither_strength;
    }

    FragColor = vec4(mixed_color, 1.0);
}