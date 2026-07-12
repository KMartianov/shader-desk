// This is pure GLSL, copied from ShaderToy (or written from scratch).
// ShaderDesk will automatically inject mainImage() and standard uniforms here.

// --- 1. ADD CUSTOM PARAMETERS VIA ANNOTATIONS ---
// Syntax: // @param name | type | default_value | Description for Lua

// @param glow_speed | float | 2.5 | Color shifting speed
// @param base_color | vec3 | 0.2, 0.5, 0.8 | Base background color
// @param enable_grid | bool | true | Enable geometric grid
// @param grid_scale | int | 10 | Geometric grid density

// --- 2. SHADERTOY CODE ---
void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
    // Normalized pixel coordinates (0 to 1)
    vec2 uv = fragCoord / iResolution.xy;
    
    // Account for aspect ratio
    uv.x *= iResolution.x / iResolution.y;

    // Use OUR custom parameters declared above:
    vec3 col = base_color;

    // Animation based on iTime and our glow_speed
    col += 0.2 * cos(iTime * glow_speed + uv.xyx + vec3(0, 2, 4));

    // Grid
    if (enable_grid) {
        vec2 grid = fract(uv * float(grid_scale));
        if (grid.x < 0.05 || grid.y < 0.05) {
            col += vec3(0.5); // Grid highlight
        }
    }

    // Mouse reaction (iMouse from Wayland)
    float distToMouse = distance(uv, iMouse.xy / iResolution.y);
    if (distToMouse < 0.2) {
        col += vec3(1.0, 0.5, 0.0) * (0.2 - distToMouse);
    }

    // Pixel output
    fragColor = vec4(col, 1.0);
}