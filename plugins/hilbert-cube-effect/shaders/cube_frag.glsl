#version 300 es
precision mediump float;

// Color passed from C++ code.
uniform vec3 line_color;

// Output fragment color.
out vec4 FragColor;

void main()
{
    FragColor = vec4(line_color, 1.0);
}