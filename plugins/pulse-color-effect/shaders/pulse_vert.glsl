// plugins/pulse-color-effect/shaders/pulse_vert.glsl
#version 300 es
precision mediump float;

// No 'in' attributes are needed from the C++ side.

// Output to the fragment shader
out vec2 v_uv;

void main() {
    // Generate a fullscreen triangle using the vertex ID
    // Maps vertex IDs 0, 1, 2 to screen corners
    float x = float((gl_VertexID & 1) << 2) - 1.0;
    float y = float((gl_VertexID & 2) << 1) - 1.0;

    // Pass UV coordinates to the fragment shader
    v_uv = vec2(x * 0.5 + 0.5, y * 0.5 + 0.5);
    
    // Set the final position of the vertex in clip space
    gl_Position = vec4(x, y, 0.0, 1.0);
}