#version 300 es
precision mediump float;
in vec2 uv;
out vec4 FragColor;
uniform float time;

void main() {
    vec2 p = uv * 2.0 - 1.0;
    p.x *= 1.5;
    
    float intensity = 0.5 / length(p) - 0.5;
    intensity += sin(time * 2.0) * 0.1;
    
    vec3 color = vec3(1.0, 0.5, 0.0) * intensity + 
                 vec3(1.0, 0.8, 0.0) * intensity * 0.5;
    
    FragColor = vec4(color, 1.0);
}