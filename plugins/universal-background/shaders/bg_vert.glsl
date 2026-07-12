#version 300 es
out vec2 v_uv;
void main() {
    // Fullscreen triangle generation without VBO
    float x = -1.0 + float((gl_VertexID & 1) << 2);
    float y = -1.0 + float((gl_VertexID & 2) << 1);
    v_uv = vec2(x * 0.5 + 0.5, y * 0.5 + 0.5);
    gl_Position = vec4(x, y, 0.999, 1.0); // Furthest Z (background)
}