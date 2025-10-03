// shaders/sphere_frag.glsl
#version 300 es
precision mediump float;

in vec3 FragPos;
in vec3 Normal;
in float Phase;

uniform vec3 wireframe_color;      // Цвет для режима сетки
uniform vec3 background_color;     // Если используется
uniform bool is_wireframe_pass;    // Флаг, указывающий на режим сетки

// Униформы для освещения
uniform vec3 lightColor;
uniform vec3 lightPos;
uniform vec3 viewPos;

out vec4 FragColor;

void main()
{
    if (is_wireframe_pass) {
        // Режим сетки - используем wireframe_color
        FragColor = vec4(wireframe_color, 1.0);
    } else {
        // Режим поверхности - стандартная логика освещения
        
        // Пример простого освещения
        vec3 norm = normalize(Normal);
        vec3 lightDir = normalize(lightPos - FragPos);
        
        // Ambient
        float ambientStrength = 0.3;
        vec3 ambient = ambientStrength * lightColor;
        
        // Diffuse
        float diff = max(dot(norm, lightDir), 0.0);
        vec3 diffuse = diff * lightColor;
        
        // Specular
        float specularStrength = 0.5;
        vec3 viewDir = normalize(viewPos - FragPos);
        vec3 reflectDir = reflect(-lightDir, norm);  
        float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32.0);
        vec3 specular = specularStrength * spec * lightColor;  
            
        vec3 result = (ambient + diffuse + specular);
        FragColor = vec4(result, 1.0);
    }
}