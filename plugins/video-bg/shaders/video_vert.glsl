#version 300 es
precision highp float;

out vec2 v_uv;       
out vec2 v_sdf_pos;  

uniform vec2 resolution;
uniform vec2 base_image_resolution;

uniform float scale;
uniform vec2 offset;
uniform float rotation;

void main() {
    // 1. Полноэкранный Quad
    vec2 pos = vec2(
        (gl_VertexID == 1 || gl_VertexID == 3) ? 1.0 : -1.0,
        (gl_VertexID == 2 || gl_VertexID == 3) ? 1.0 : -1.0
    );

    v_sdf_pos = pos;

    // 2. Трансформации
    vec2 transformed_pos = pos;
    transformed_pos *= scale;

    if (abs(rotation) > 0.001) {
        float rad = radians(rotation);
        float c = cos(rad);
        float s = sin(rad);
        transformed_pos = mat2(c, -s, s, c) * transformed_pos;
    }

    transformed_pos += offset * 2.0;

    // 3. Стандартный маппинг координат
    vec2 uv = pos * 0.5 + 0.5;
    
    // Переворот оси Y для корректного отображения (Top-Left -> Bottom-Left)
    uv.y = 1.0 - uv.y; 

    v_uv = uv;
    gl_Position = vec4(transformed_pos, 0.5, 1.0);
}