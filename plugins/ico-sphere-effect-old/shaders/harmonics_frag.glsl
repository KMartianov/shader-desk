#version 300 es
// High precision is mandatory for normal calculations
precision highp float; 

in vec3 FragPos;

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
    // --- MAIN MODE: Wireframe ---
    if (is_wireframe_pass) {
        // Render as pure emissive neon lines. Distance fading is removed.
        FragColor = vec4(wireframe_color, 1.0);
        return;
    }

    // --- FALLBACK MODE: Solid Sphere ---
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

    // Diffuse lighting
    float diff = max(dot(normal, lightDir), 0.0);
    
    // 60% Ambient base so the sphere is always vibrant and visible
    vec3 ambient = 0.6 * object_color;
    // 40% Directional light for 3D volume
    vec3 diffuse = diff * lightColor * 0.4 * object_color;

    // Edge highlighting (Fresnel)
    // Exponent lowered to 2.0 to make the glowing rim wider and softer
    float fresnel = pow(1.0 - max(dot(normal, viewDir), 0.0), 2.0);
    vec3 rimLight = fresnel * wireframe_color * 1.2; 

    // Final composition
    vec3 result = ambient + diffuse + rimLight;
    FragColor = vec4(result, 1.0);
}