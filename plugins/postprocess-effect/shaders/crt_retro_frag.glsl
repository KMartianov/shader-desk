#version 300 es
precision highp float;

in vec2 v_uv;
out vec4 FragColor;

uniform sampler2D u_prev_layer;
uniform vec2 resolution;
uniform float time;

uniform float intensity; 
uniform float speed;     
uniform float scale;     
uniform int variant;     // 0: Color, 1: Green Terminal, 2: Black & White

vec2 curve_uv(vec2 uv, float warp) {
    uv = uv * 2.0 - 1.0; 
    vec2 offset = abs(uv.yx) / vec2(warp, warp);
    uv = uv + uv * offset * offset;
    uv = uv * 0.5 + 0.5; 
    return uv;
}

void main() {
    // 1. Lens Distortion
    float warp_factor = 4.0 - clamp(intensity, 0.0, 1.0) * 2.0; 
    vec2 uv = curve_uv(v_uv, warp_factor);

    // Draw transparent boundaries outside the "tube"
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        FragColor = vec4(0.0);
        return;
    }

    // 2. Chromatic Aberration
    float color_split = (length(uv - 0.5) * 0.02) * intensity;
    float jitter = sin(time * speed * 10.0) * 0.002 * intensity;
    
    vec2 uv_r = vec2(uv.x + color_split, uv.y + jitter);
    vec2 uv_g = vec2(uv.x,               uv.y);
    vec2 uv_b = vec2(uv.x - color_split, uv.y - jitter);

    vec4 tex_r = texture(u_prev_layer, uv_r);
    vec4 tex_g = texture(u_prev_layer, uv_g);
    vec4 tex_b = texture(u_prev_layer, uv_b);

    vec3 col = vec3(tex_r.r, tex_g.g, tex_b.b);
    
    // Alpha Propagation
    float final_alpha = max(tex_r.a, max(tex_g.a, tex_b.a));

    // 3. Scanlines
    // Multiply by final_alpha to prevent scanlines from rendering on empty space
    float scanline = sin(uv.y * resolution.y * (scale * 0.1)) * 0.04;
    col -= (scanline * final_alpha);

    // 4. Vignetting
    float vignette = length(uv - 0.5);
    col *= 1.0 - vignette * 0.5;

    // 5. Color Filters
    if (variant == 1) {
        float lum = dot(col, vec3(0.299, 0.587, 0.114));
        col = vec3(0.05, lum * 1.2, 0.05);
    } else if (variant == 2) {
        float lum = dot(col, vec3(0.299, 0.587, 0.114));
        col = vec3(lum);
    }

    // Phosphor glow
    col *= 1.2;

    FragColor = vec4(col, final_alpha);
}