// Это чистый GLSL, скопированный с ShaderToy (или написанный с нуля).
// ShaderDesk автоматически добавит сюда mainImage() и стандартные uniforms.

// --- 1. ДОБАВЛЯЕМ СВОИ ПАРАМЕТРЫ ЧЕРЕЗ АННОТАЦИИ ---
// Синтаксис: // @param имя | тип | значение_по_умолчанию | Описание для Lua

// @param glow_speed | float | 2.5 | Скорость переливания цвета
// @param base_color | vec3 | 0.2, 0.5, 0.8 | Базовый цвет фона
// @param enable_grid | bool | true | Включить геометрическую сетку
// @param grid_scale | int | 10 | Плотность геометрической сетки

// --- 2. КОД SHADERTOY ---
void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
    // Нормализованные координаты пикселя (от 0 до 1)
    vec2 uv = fragCoord / iResolution.xy;
    
    // Учитываем соотношение сторон
    uv.x *= iResolution.x / iResolution.y;

    // Используем НАШИ кастомные параметры, объявленные выше:
    vec3 col = base_color;

    // Анимация на основе iTime и нашего glow_speed
    col += 0.2 * cos(iTime * glow_speed + uv.xyx + vec3(0, 2, 4));

    // Сетка
    if (enable_grid) {
        vec2 grid = fract(uv * float(grid_scale));
        if (grid.x < 0.05 || grid.y < 0.05) {
            col += vec3(0.5); // Подсветка сетки
        }
    }

    // Реакция на мышь (iMouse из Wayland)
    float distToMouse = distance(uv, iMouse.xy / iResolution.y);
    if (distToMouse < 0.2) {
        col += vec3(1.0, 0.5, 0.0) * (0.2 - distToMouse);
    }

    // Вывод пикселя
    fragColor = vec4(col, 1.0);
}