#version 300 es
precision highp float;

out vec2 v_uv;

void main() {
    // Генерация полноэкранного треугольника без VBO
    float x = -1.0 + float((gl_VertexID & 1) << 2);
    float y = -1.0 + float((gl_VertexID & 2) << 1);
    
    // UV координаты от 0.0 до 1.0
    // Переворачиваем Y для корректной ориентации (Top-Left = 0,0)
    v_uv = vec2(x * 0.5 + 0.5, 1.0 - (y * 0.5 + 0.5));
    
    // Z = 0.999 (максимальная глубина, фон)
    gl_Position = vec4(x, y, 0.999, 1.0);
}