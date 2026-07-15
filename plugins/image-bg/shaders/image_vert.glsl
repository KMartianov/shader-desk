#version 300 es
precision highp float; // <--- ДЛЯ СОВМЕСТИМОСТИ
precision highp int;   // <--- ДЛЯ СОВМЕСТИМОСТИ

// Вершинный шейдер. Выполняется всего 4 раза за кадр!

out vec2 v_uv;

uniform vec2 resolution;
uniform vec2 base_image_resolution;
uniform int fill_mode;
uniform float scale;
uniform vec2 offset;
uniform float rotation;

void main() {
    // Генерируем базовый прямоугольник (Quad) от -1.0 до 1.0
    vec2 pos = vec2(
        (gl_VertexID == 1 || gl_VertexID == 3) ? 1.0 : -1.0,
        (gl_VertexID == 2 || gl_VertexID == 3) ? 1.0 : -1.0
    );
    
    vec2 uv = pos * 0.5 + 0.5;
    uv.y = 1.0 - uv.y; // Переворот для текстур

    float screen_aspect = resolution.x / resolution.y;
    float tex_aspect = base_image_resolution.x / base_image_resolution.y;

    // ==============================================================================
    // ГЕОМЕТРИЧЕСКОЕ ОТСЕЧЕНИЕ (ОПТИМИЗАЦИЯ FILLRATE)
    // ==============================================================================
    if (fill_mode == 1) { 
        // РЕЖИМ CONTAIN (Спрайты: Девушка, Котик). 
        // Мы физически сжимаем прямоугольник до размеров картинки.
        // Растеризатор GPU вообще не будет трогать пустые пиксели экрана!
        vec2 geom_scale = vec2(1.0);
        if (screen_aspect > tex_aspect) geom_scale.x = tex_aspect / screen_aspect;
        else                            geom_scale.y = screen_aspect / tex_aspect;
        
        pos *= geom_scale;
        pos *= scale;

        // Поворот и сдвиг применяются к геометрии, а не к UV
        if (abs(rotation) > 0.001) {
            float rad = radians(rotation);
            float c = cos(rad), s = sin(rad);
            pos = mat2(c, -s, s, c) * pos;
        }
        pos += offset * 2.0; 
        
        v_uv = uv; // UV остается идеальным (0.0 - 1.0)
        
    } else {
        // РЕЖИМ COVER / TILE (Фон).
        // Оставляем геометрию на весь экран, но манипулируем UV координатами.
        if (fill_mode == 0) { // Cover
            uv -= 0.5;
            if (screen_aspect > tex_aspect) uv.y *= (resolution.y / resolution.x) * tex_aspect;
            else                            uv.x *= (resolution.x / resolution.y) / tex_aspect;
            uv += 0.5;
        }
        
        uv -= 0.5;
        uv /= scale;
        if (abs(rotation) > 0.001) {
            float rad = radians(rotation);
            float c = cos(rad), s = sin(rad);
            uv = mat2(c, -s, s, c) * uv; 
        }
        uv += 0.5;
        uv -= offset;
        
        v_uv = uv;
    }
    
    gl_Position = vec4(pos, 0.999, 1.0);
}