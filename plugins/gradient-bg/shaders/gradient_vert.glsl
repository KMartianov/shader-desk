#version 300 es

out vec2 v_uv;

void main() {
    // Generate a fullscreen triangle on the fly (without VBO)
    float x = -1.0 + float((gl_VertexID & 1) << 2);
    float y = -1.0 + float((gl_VertexID & 2) << 1);
    
    // Pass normalized UV coordinates (0.0 - 1.0) to the fragment shader
    v_uv = vec2(x * 0.5 + 0.5, y * 0.5 + 0.5);
    
    // Send the triangle to the furthest Z-plane (0.999) so it acts as a background
    gl_Position = vec4(x, y, 0.999, 1.0);
}
