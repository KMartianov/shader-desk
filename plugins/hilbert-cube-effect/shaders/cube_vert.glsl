#version 300 es

// (location = 0): Позиция вершины в локальных координатах.
layout (location = 0) in vec3 aPos;

// Uniform-переменные: матрицы для преобразования координат.
uniform mat4 model;      // Матрица модели (поворот куба)
uniform mat4 view;       // Матрица вида (камера)
uniform mat4 projection; // Матрица проекции

void main()
{
    // Вычисляем финальную позицию вершины на экране.
    gl_Position = projection * view * model * vec4(aPos, 1.0);
}