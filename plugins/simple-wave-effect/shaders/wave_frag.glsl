// Plugins/simple-wave-effect/shaders/wave_frag.glsl
#version 300 es
precision highp float;

// Input data from the vertex shader (texel coordinates)
in vec2 v_uv;

// Output color
out vec4 FragColor;

// Standard uniform variables, always available
uniform float time;
uniform vec2 resolution;

// Custom parameters that will be controlled from the config.
// Format: @param <code_name> | <type> | <default_value> | <description>

// @param wave_color | vec3 | 0.1, 0.5, 1.0 | Main wave color (R,G,B)
uniform vec3 wave_color;

// @param speed | float | 2.5 | Wave movement speed
uniform float speed;

// @param frequency | float | 20.0 | Wave frequency (count) on the screen
uniform float frequency;

// @param is_inverted | bool | false | Whether to invert colors
uniform bool is_inverted;

void main() {
    // Normalize coordinates so the center is (0,0)
    vec2 uv = v_uv - 0.5;
    
    // Simple wave equation depending on time and distance from the center
    float wave = sin(length(uv) * frequency - time * speed);
    
    // Make the waves smoother
    wave = smoothstep(0.0, 1.0, wave);
    
    vec3 final_color = wave_color * wave;
    
    if (is_inverted) {
        final_color = vec3(1.0) - final_color;
    }
    
    FragColor = vec4(final_color, 1.0);
}