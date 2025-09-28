#version 300 es
precision mediump float;
layout(location = 0) in vec2 aPos;
out vec2 vTexCoord;

void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    vTexCoord = aPos * 0.5 + 0.5;
}