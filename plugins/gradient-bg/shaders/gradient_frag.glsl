#version 300 es
// Use highp, because distance calculations require high precision,
// Otherwise floating-point artifacts might appear on gradients.
precision highp float;

in vec2 v_uv;
out vec4 FragColor;

// ==============================================================================
// 1. AUTO-GENERATED PARAMETERS (For the generate_plugin.py script)
// ==============================================================================

// @param blend_power | float | 2.5 | Color blending power (1.0 - hard, 3.0+ - liquid)
uniform float blend_power;
// @param bg_color | vec3 | 0.05, 0.05, 0.08 | Base background color (void)
uniform vec3 bg_color;

// --- Contour settings (Topography) ---
// @param enable_stripes | bool | false | Enable topographic lines
uniform bool enable_stripes;
// @param stripes_density | float | 15.0 | Contour line density
uniform float stripes_density;
// @param stripes_opacity | float | 0.15 | Contour line opacity
uniform float stripes_opacity;

// --- Render settings ---
// @param dithering_amount | float | 0.02 | Anti-banding noise (color stepping prevention)
uniform float dithering_amount;

// ==============================================================================
// 2. DYNAMIC DATA (Read from BlackBoard, configured in C++)
// ==============================================================================

uniform vec2 resolution; // Passed from C++ template

#define MAX_POINTS 16
uniform int active_points;                    // How many points are actually used
uniform vec2 point_positions[MAX_POINTS];     // UV coordinates of points (0.0 - 1.0)
uniform vec3 point_colors[MAX_POINTS];        // RGB colors
uniform float point_radii[MAX_POINTS];        // Influence radii

// ==============================================================================
// 3. UTILITIES
// ==============================================================================

// Fast pseudo-random generator for dithering
float rand(vec2 co) {
    return fract(sin(dot(co.xy ,vec2(12.9898,78.233))) * 43758.5453);
}

void main() {
    // Aspect ratio correction (so circles don't squash into ovals)
    float aspect = resolution.x / resolution.y;
    vec2 pos = v_uv;
    pos.x = (pos.x - 0.5) * aspect + 0.5;

    vec3 total_color = vec3(0.0);
    float total_weight = 0.0001; // Protection against division by zero in empty areas

    // ========================================================================
    // STEP 1: CALCULATE THE INTENSITY FIELD (METABALLS / IDW)
    // ========================================================================
    for(int i = 0; i < MAX_POINTS; i++) {
        // Optimization: exit the loop if all active points have been processed
        if (i >= active_points) break; 

        vec2 pt_pos = point_positions[i];
        pt_pos.x = (pt_pos.x - 0.5) * aspect + 0.5; // Correct point position

        // Distance from the current pixel to the point
        float dist = distance(pos, pt_pos);
        dist = max(dist, 0.001); // Protection against division by zero right at the point's center

        // Calculate the weight (influence) of this point on the current pixel.
        // The larger the radius and smaller the distance, the higher the weight.
        float weight = pow(point_radii[i] / dist, blend_power);

        total_weight += weight;
        total_color += point_colors[i] * weight;
    }

    // Final pixel color (Weighted average + smooth fade to background color)
    vec3 final_color = total_color / total_weight;
    
    // Mix with the background where the total weight is less than 1.0 (outside radii)
    final_color = mix(bg_color, final_color, clamp(total_weight, 0.0, 1.0));

    // ========================================================================
    // STEP 2: CONTOUR LINES (TOPOGRAPHY / STRIPES)
    // ========================================================================
    if (enable_stripes) {
        // Use the logarithm of the total weight so the field distributes 
        // More evenly and lines won't clump together near point centers.
        float field = log(total_weight + 1.0); 
        
        // Generate a wave based on the field
        float wave = sin(field * stripes_density);
        
        // Create thin hard lines (leave only the sine wave peaks)
        float line = smoothstep(0.9, 1.0, wave);
        
        // Overlay lines on top of the gradient (light and semi-transparent)
        final_color = mix(final_color, vec3(1.0), line * stripes_opacity);
    }

    // ========================================================================
    // STEP 3: DITHERING
    // ========================================================================
    // Extract microscopic noise [-0.5..0.5] for each pixel.
    // This breaks up 8-bit gradient banding when displayed on the monitor.
    float noise = (rand(gl_FragCoord.xy) - 0.5) * dithering_amount;
    final_color += noise;

    FragColor = vec4(final_color, 1.0);
}
