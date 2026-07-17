#version 300 es
precision highp float;

in vec2 v_uv;
out vec4 FragColor;

uniform sampler2D u_prev_layer;
uniform vec2 resolution;

// ==============================================================================
// AUTO-GENERATED PARAMETERS
// ==============================================================================

// @param dither_spread | float | 0.5 | Noise intensity (0.0 = banding, 1.0 = heavy noise)
uniform float dither_spread;

// @param downsample_scale | float | 3.0 | Pixel size (1.0 = native, 4.0 = retro)
uniform float downsample_scale;

// @param bayer_size | int | 1 | Bayer Matrix resolution: 0 = 2x2, 1 = 4x4, 2 = 8x8
uniform int bayer_size;

// @param colors_count | int | 4 | Number of active colors in the palette (2 to 16)
uniform int colors_count;

// ==============================================================================
// BLACKBOARD DATA (Dynamic Array)
// ==============================================================================
uniform vec3 palette[16];

// ==============================================================================
// BAYER MATRICES
// ==============================================================================
const float bayer2[4] = float[](
    0.0/4.0, 2.0/4.0,
    3.0/4.0, 1.0/4.0
);

const float bayer4[16] = float[](
    0.0/16.0,  8.0/16.0,  2.0/16.0, 10.0/16.0,
   12.0/16.0,  4.0/16.0, 14.0/16.0,  6.0/16.0,
    3.0/16.0, 11.0/16.0,  1.0/16.0,  9.0/16.0,
   15.0/16.0,  7.0/16.0, 13.0/16.0,  5.0/16.0
);

const float bayer8[64] = float[](
    0.0/64.0, 48.0/64.0, 12.0/64.0, 60.0/64.0,  3.0/64.0, 51.0/64.0, 15.0/64.0, 63.0/64.0,
   32.0/64.0, 16.0/64.0, 44.0/64.0, 28.0/64.0, 35.0/64.0, 19.0/64.0, 47.0/64.0, 31.0/64.0,
    8.0/64.0, 56.0/64.0,  4.0/64.0, 52.0/64.0, 11.0/64.0, 59.0/64.0,  7.0/64.0, 55.0/64.0,
   40.0/64.0, 24.0/64.0, 36.0/64.0, 20.0/64.0, 43.0/64.0, 27.0/64.0, 39.0/64.0, 23.0/64.0,
    2.0/64.0, 50.0/64.0, 14.0/64.0, 62.0/64.0,  1.0/64.0, 49.0/64.0, 13.0/64.0, 61.0/64.0,
   34.0/64.0, 18.0/64.0, 46.0/64.0, 30.0/64.0, 33.0/64.0, 17.0/64.0, 45.0/64.0, 29.0/64.0,
   10.0/64.0, 58.0/64.0,  6.0/64.0, 54.0/64.0,  9.0/64.0, 57.0/64.0,  5.0/64.0, 53.0/64.0,
   42.0/64.0, 26.0/64.0, 38.0/64.0, 22.0/64.0, 41.0/64.0, 25.0/64.0, 37.0/64.0, 21.0/64.0
);

void main() {
    float p_size = max(1.0, floor(downsample_scale));
    vec2 frag_coord = v_uv * resolution;
    vec2 pix_coord = floor(frag_coord / p_size);
    vec2 pix_uv = (pix_coord * p_size + (p_size * 0.5)) / resolution;
    
    vec3 col = texture(u_prev_layer, pix_uv).rgb;
    float lum = dot(col, vec3(0.299, 0.587, 0.114));

    float bayer_val = 0.0;
    if (bayer_size == 0) {
        int bx = int(mod(pix_coord.x, 2.0));
        int by = int(mod(pix_coord.y, 2.0));
        bayer_val = bayer2[bx + by * 2];
    } else if (bayer_size == 1) {
        int bx = int(mod(pix_coord.x, 4.0));
        int by = int(mod(pix_coord.y, 4.0));
        bayer_val = bayer4[bx + by * 4];
    } else {
        int bx = int(mod(pix_coord.x, 8.0));
        int by = int(mod(pix_coord.y, 8.0));
        bayer_val = bayer8[bx + by * 8];
    }

    float dither_offset = (bayer_val - 0.5) * dither_spread;
    float dithered_lum = clamp(lum + dither_offset, 0.0, 1.0);

    // ========================================================================
    // O(1) PALETTE MAPPING (NO IF-ELSE BRANCHING)
    // ========================================================================
    int active_colors = clamp(colors_count, 2, 16);
    float scaled_lum = dithered_lum * float(active_colors - 1);
    
    // Map luminance directly to an array index
    int final_idx = clamp(int(floor(scaled_lum + 0.5)), 0, active_colors - 1);

    FragColor = vec4(palette[final_idx], 1.0);
}