#version 300 es
precision mediump float;
in vec2 uv;
out vec4 FragColor;
uniform float time;

void main() {
    vec2 p = uv * 2.0 - 1.0;
    float wave = sin(length(p * 5.0) - time * 3.0);
    wave += sin(p.x * 10.0 + time * 2.0) * 0.3;
    wave += sin(p.y * 8.0 + time * 1.5) * 0.3;
    
    vec3 color = vec3(0.1, 0.3, 0.8) * (wave * 0.5 + 0.5);
    
    FragColor = vec4(color, 1.0);
}