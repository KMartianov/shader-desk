#version 300 es
precision mediump float;
in vec2 v_uv;
out vec4 FragColor;

uniform vec3 color_top;
uniform vec3 color_bottom;
uniform int gradient_type; // 0 = Solid, 1 = Vertical, 2 = Radial
uniform float time;
uniform float pulse_speed;

void main() {
    vec3 col = color_top;
    
    if (gradient_type == 1) {
        // Вертикальный градиент
        col = mix(color_bottom, color_top, smoothstep(0.0, 1.0, v_uv.y));
    } else if (gradient_type == 2) {
        // Радиальный (виньетка из центра)
        float dist = distance(v_uv, vec2(0.5));
        col = mix(color_top, color_bottom, smoothstep(0.0, 0.8, dist));
    }
    
    // Легкая анимация дыхания фона
    if (pulse_speed > 0.001) {
        col += sin(time * pulse_speed) * 0.03;
    }

    FragColor = vec4(col, 1.0); // Фон всегда имеет Alpha = 1.0
}