#version 300 es
precision highp float;

out vec2 v_uv;

void main() {
    // Generate a fullscreen triangle without VBO
    float x = -1.0 + float((gl_VertexID & 1) << 2);
    float y = -1.0 + float((gl_VertexID & 2) << 1);
    
    // UV coordinates (0.0 .. 1.0). 
    // NO Y-axis flip here! The previous FBO layer is already correctly oriented in OpenGL space.
    v_uv = vec2(x * 0.5 + 0.5, y * 0.5 + 0.5);
    
    gl_Position = vec4(x, y, 0.0, 1.0);
}