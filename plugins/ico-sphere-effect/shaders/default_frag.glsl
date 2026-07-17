#version 300 es
// High precision is mandatory for normal calculations
precision highp float; 

in vec3 FragPos;
in vec3 vBary;  

// --- Parameters from C++ / Lua (Do not delete comments!) ---
// @param is_wireframe_pass | bool | false | Wireframe render flag.
uniform bool is_wireframe_pass;
// @param wireframe_color | vec3 | 0.5, 0.5, 0.7 | Wireframe color.
uniform vec3 wireframe_color;
// @param object_color | vec3 | 0.08, 0.12, 0.20 | Solid sphere surface color.
uniform vec3 object_color;

// Uniforms for lighting
uniform vec3 lightColor;
uniform vec3 lightPos;
uniform vec3 viewPos;

out vec4 FragColor;

void main()
{
    // ========================================================================
    // MAIN MODE: Wireframe (Barycentric Wireframe)
    // ========================================================================
    if (is_wireframe_pass) {
        // 1. Find the distance from the current pixel to the nearest triangle edge
        float minBary = min(min(vBary.x, vBary.y), vBary.z);
        
        // 2. Calculate the line thickness in pixels. 
        // The fwidth function ensures the line is exactly 1.2 pixels wide 
        // Regardless of whether the sphere is close to or far from the camera!
        float edgeWidth = fwidth(minBary) * 1.2; 
        
        // 3. CRITICAL STEP: If the pixel is inside the polygon 
        // (further from the edge than the line thickness) — discard it completely!
        if (minBary > edgeWidth) {
            discard;
        }

        // Distance fogFactor completely removed! 
        // Emissive wireframes are now bright at any distance from the Global Camera.
        FragColor = vec4(wireframe_color, 1.0);
        return;
    }

    // ========================================================================
    // FALLBACK MODE: Solid Sphere
    // Enabled if wireframe_mode = false. Looks like a Low-Poly crystal.
    // ========================================================================

    // Hardware triangle normal calculation (Flat Shading)
    vec3 dx = dFdx(FragPos);
    vec3 dy = dFdy(FragPos);
    vec3 normal = normalize(cross(dx, dy));

    vec3 viewDir = normalize(viewPos - FragPos);
    
    // ========================================================================
    // FBO Y-FLIP PROTECTION
    // Ensures the normal always points outwards towards the camera, regardless
    // of Wayland compositor or OpenGL driver framebuffer orientations.
    // ========================================================================
    if (dot(normal, viewDir) < 0.0) {
        normal = -normal;
    }

    vec3 lightDir = normalize(lightPos - FragPos);

    // Diffuse lighting calculation
    float diff = max(dot(normal, lightDir), 0.0);
    
    // Boosted ambient to 60% ensures the dark side of the planet doesn't become pitch black
    // The base color is multiplied here directly
    vec3 ambient = 0.6 * lightColor;
    vec3 diffuse = diff * lightColor * 0.4;

    // Edge highlighting (Fresnel effect)
    // Exponent lowered to 2.0 to make the glowing rim wider and softer
    float fresnel = pow(1.0 - max(dot(normal, viewDir), 0.0), 2.0);
    vec3 rimLight = fresnel * wireframe_color * 1.2; 

    // Final color composition
    vec3 result = (ambient + diffuse) * object_color + rimLight;
    
    FragColor = vec4(result, 1.0);
}