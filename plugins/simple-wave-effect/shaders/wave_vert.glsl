// Plugins/simple-wave-effect/shaders/wave_vert.glsl
#version 300 es

// Output data for the fragment shader
out vec2 v_uv;

// Vertex array for the fullscreen triangle
const vec2 positions[3] = vec2[](
    vec2(-1.0, -1.0),
    vec2(3.0, -1.0),
    vec2(-1.0, 3.0)
);

void main() {
    // Select vertex
    vec2 pos = positions[gl_VertexID];
    
    // Pass UV coordinates to the fragment shader
    // (0,0) in the bottom-left corner, (1,1) in the top-right
    v_uv = pos * 0.5 + 0.5;
    
    // Set vertex position
    gl_Position = vec4(pos, 0.0, 1.0);
}