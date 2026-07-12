#version 300 es
// High precision is mandatory
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
    // Calculate the distance from the camera (viewPos) to the current fragment
    float distance = length(viewPos - FragPos);

    // ========================================================================
    // MAIN MODE: Wireframe
    // ========================================================================
    if (is_wireframe_pass) {
        // Depth Fading effect.
        // 2.0 - start of fading (front part of the sphere)
        // 2.5 - distance at which the wireframe darkens the most
        // 0.2 - minimum brightness (so back lines don't disappear completely)
        float fogFactor = clamp(1.0 - (distance - 2.0) / 2.5, 0.2, 1.0);
        
        vec3 finalWireColor = wireframe_color * fogFactor;
        FragColor = vec4(finalWireColor, 1.0);
        return;
    }

    // ========================================================================
    // FALLBACK MODE: Solid Sphere (Solid Mode / Low-Poly)
    // (Activated if p.wireframe_mode = false is set in Lua)
    // ========================================================================
    
    // Hardware face normal calculation (makes the geometry faceted/Low-Poly)
    vec3 dx = dFdx(FragPos);
    vec3 dy = dFdy(FragPos);
    vec3 normal = normalize(cross(dx, dy));

    vec3 viewDir = normalize(viewPos - FragPos);
    vec3 lightDir = normalize(lightPos - FragPos);

    // Lighting (Ambient + Diffuse)
    vec3 ambient = 0.2 * lightColor;
    float diff = max(dot(normal, lightDir), 0.0);
    vec3 diffuse = diff * lightColor;

    // Fresnel effect (Glowing edges at an angle, using the wireframe color for styling)
    float fresnel = pow(1.0 - max(dot(normal, viewDir), 0.0), 3.0);
    vec3 rimLight = fresnel * wireframe_color * 0.8; 

    // Color assembly
    vec3 result = (ambient + diffuse) * object_color + rimLight;
    
    FragColor = vec4(result, 1.0);
}