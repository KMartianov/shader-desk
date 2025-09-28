#version 300 es
precision mediump float;
in vec2 uv;
out vec4 FragColor;
uniform float time;

void main() {
    float plasma = sin(uv.x * 10.0 + time) * 
                   sin(uv.y * 10.0 + time) * 
                   sin((uv.x + uv.y) * 5.0 + time);
    
    vec3 color = 0.5 + 0.5 * cos(plasma * 3.0 + vec3(0, 2, 4));
    FragColor = vec4(color, 1.0);
}