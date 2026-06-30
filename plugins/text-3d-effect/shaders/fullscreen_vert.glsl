#version 300 es

// Трюк для создания полноэкранного полигона без использования VBO/VAO.
// Экономит память и CPU-вызовы. Идеально для Raymarching'а.
void main() {
    float x = -1.0 + float((gl_VertexID & 1) << 2);
    float y = -1.0 + float((gl_VertexID & 2) << 1);
    gl_Position = vec4(x, y, 0.0, 1.0);
}