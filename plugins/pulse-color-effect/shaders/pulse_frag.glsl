// plugins/pulse-color-effect/shaders/pulse_frag.glsl
#version 300 es
precision mediump float;

// Input from the vertex shader
in vec2 v_uv;

// Output color
out vec4 FragColor;

// Uniforms for customization
uniform float time;
uniform vec2 resolution;

// @param base_color | vec3 | 0.2,0.6,1.0 | Базовый цвет эффекта (R,G,B)
uniform vec3 base_color;

// @param pulse_speed | float | 1.0 | Скорость пульсации
uniform float pulse_speed;

// @param pulse_strength | float | 0.5 | Амплитуда цветовой пульсации
uniform float pulse_strength;

// @param color_shift_mode | bool | true | Включить цветовой сдвиг с фазой
uniform bool color_shift_mode;

void main() {
    // Center the coordinates
    vec2 p = v_uv - 0.5;
    
    // Create a radial pulse effect
    float len = length(p);
    float pulse = sin(len * 10.0 - time * pulse_speed);
    
    // Smoothly clamp the pulse to be positive
    pulse = smoothstep(0.0, 1.0, pulse);
    
    vec3 color = base_color;

    if (color_shift_mode) {
        // Shift color based on the angle
        float angle = atan(p.y, p.x);
        color.r += sin(angle * 2.0 + time * 0.5) * 0.2;
        color.g += cos(angle * 3.0 + time * 0.4) * 0.2;
    }
    
    // Combine the base color with the pulse
    vec3 final_color = color + color * pulse * pulse_strength;
    
    FragColor = vec4(final_color, 1.0);
}