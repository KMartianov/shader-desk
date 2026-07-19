#version 300 es
precision highp float;

in vec2 v_uv;
out vec4 FragColor;

// --- System Uniforms ---
uniform sampler2D u_prev_layer; 
uniform float time;
uniform vec2 resolution;

// ==============================================================================
// UNIVERSAL PARAMETERS
// ==============================================================================
// @param intensity | float | 0.3 | Glitch and RGB split intensity
uniform float intensity;
// @param speed | float | 2.0 | Artifact change speed
uniform float speed;
// @param scale | float | 15.0 | Glitch block density (higher = smaller blocks)
uniform float scale;
// @param variant | int | 1 | 0: Smooth RGB, 1: Block VHS, 2: Hardcore Scanlines
uniform int variant;

// ==============================================================================
// MATH UTILITIES
// ==============================================================================
float rand(vec2 co) {
    return fract(sin(dot(co.xy ,vec2(12.9898,78.233))) * 43758.5453);
}

void main() {
    vec2 uv = v_uv;
    float t = floor(time * speed * 10.0); // Discrete time for jittery animation
    
    float split_dist = intensity * 0.03; // Base color shift

    // --- GLITCH LOGIC ---
    if (variant == 1 || variant == 2) {
        float block = floor(uv.y * scale); 
        float block_noise = rand(vec2(t, block)); 
        
        if (block_noise > 1.0 - (intensity * 0.5)) {
            float shift = (rand(vec2(block, t + 1.0)) - 0.5) * intensity * 0.1;
            uv.x += shift;
            split_dist *= 2.0; // Amplify RGB split in broken blocks
        }
        
        if (variant == 2) {
            float scanline = sin(uv.y * resolution.y * 1.5) * 0.04 * intensity;
            uv.x += scanline * block_noise;
        }
    }

    // --- CHROMATIC ABERRATION (RGB Split) ---
    // CRITICAL FIX: Removed redeclarations and applied clamping correctly.
    vec2 uv_r = clamp(uv + vec2(split_dist, 0.0), 0.0, 1.0);
    vec2 uv_b = clamp(uv - vec2(split_dist, 0.0), 0.0, 1.0);
    uv = clamp(uv, 0.0, 1.0);

    vec4 tex_r = texture(u_prev_layer, uv_r);
    vec4 tex_g = texture(u_prev_layer, uv);
    vec4 tex_b = texture(u_prev_layer, uv_b);
    
    // CRITICAL FIX: Alpha Channel Preservation for Wireframes!
    // If the wireframe line shifts laterally, its alpha must shift with it.
    // We take the maximum alpha across all channels so the RGB trails remain visible
    // and don't get erased during final Wayland compositing.
    float final_alpha = max(tex_r.a, max(tex_g.a, tex_b.a));

    FragColor = vec4(tex_r.r, tex_g.g, tex_b.b, final_alpha);
}