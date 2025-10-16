// plugins/simple-wave-effect/shaders/wave_vert.glsl
#version 300 es

// Выходные данные для фрагментного шейдера
out vec2 v_uv;

// Массив вершин для полноэкранного треугольника
const vec2 positions[3] = vec2[](
    vec2(-1.0, -1.0),
    vec2(3.0, -1.0),
    vec2(-1.0, 3.0)
);

void main() {
    // Выбираем вершину
    vec2 pos = positions[gl_VertexID];
    
    // Передаем UV-координаты во фрагментный шейдер
    // (0,0) в нижнем левом углу, (1,1) в верхнем правом
    v_uv = pos * 0.5 + 0.5;
    
    // Устанавливаем позицию вершины
    gl_Position = vec4(pos, 0.0, 1.0);
}