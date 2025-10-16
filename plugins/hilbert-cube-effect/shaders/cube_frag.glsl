#version 300 es
precision mediump float;

// Цвет, передаваемый из C++ кода.
uniform vec3 line_color;

// Выходной цвет фрагмента.
out vec4 FragColor;

void main()
{
    FragColor = vec4(line_color, 1.0);
}