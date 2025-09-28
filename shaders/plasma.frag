#version 300 es
precision mediump float;
in vec2 vTexCoord;
out vec4 fragColor;
uniform float time;

void main() {
    vec2 uv = vTexCoord;
    float plasma = sin(uv.x * 10.0 + time) * 0.5 + 0.5;
    plasma += sin(uv.y * 8.0 + time) * 0.5 + 0.5;
    plasma += sin((uv.x + uv.y) * 5.0 + time) * 0.5 + 0.5;
    plasma = fract(plasma);
    
    vec3 color = 0.5 + 0.5 * cos(plasma * 3.14159 + vec3(0, 2, 4));
    fragColor = vec4(color, 1.0);
}