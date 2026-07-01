#version 300 es
// Обязательно высокая точность
precision highp float; 

in vec3 FragPos;

// --- Параметры из C++ / Lua (Не удалять комментарии!) ---
// @param is_wireframe_pass | bool | false | Флаг отрисовки сетки.
uniform bool is_wireframe_pass;
// @param wireframe_color | vec3 | 0.5, 0.5, 0.7 | Цвет проволочной сетки.
uniform vec3 wireframe_color;
// @param object_color | vec3 | 0.08, 0.12, 0.20 | Цвет сплошной поверхности сферы.
uniform vec3 object_color;

// Униформы для освещения
uniform vec3 lightColor;
uniform vec3 lightPos;
uniform vec3 viewPos;

out vec4 FragColor;

void main()
{
    // Вычисляем расстояние от камеры (viewPos) до текущего фрагмента
    float distance = length(viewPos - FragPos);

    // ========================================================================
    // ГЛАВНЫЙ РЕЖИМ: Проволочная сетка (Wireframe)
    // ========================================================================
    if (is_wireframe_pass) {
        // Эффект затухания в глубину (Depth Fading).
        // 2.0 - начало затухания (передняя часть сферы)
        // 2.5 - дальность, на которой сетка темнеет сильнее всего
        // 0.2 - минимальная яркость (чтобы задние линии не исчезали полностью)
        float fogFactor = clamp(1.0 - (distance - 2.0) / 2.5, 0.2, 1.0);
        
        vec3 finalWireColor = wireframe_color * fogFactor;
        FragColor = vec4(finalWireColor, 1.0);
        return;
    }

    // ========================================================================
    // РЕЗЕРВНЫЙ РЕЖИМ: Сплошная сфера (Solid Mode / Low-Poly)
    // (Активируется, если в Lua поставить p.wireframe_mode = false)
    // ========================================================================
    
    // Аппаратное вычисление нормалей граней (делает геометрию Low-Poly граненой)
    vec3 dx = dFdx(FragPos);
    vec3 dy = dFdy(FragPos);
    vec3 normal = normalize(cross(dx, dy));

    vec3 viewDir = normalize(viewPos - FragPos);
    vec3 lightDir = normalize(lightPos - FragPos);

    // Освещение (Ambient + Diffuse)
    vec3 ambient = 0.2 * lightColor;
    float diff = max(dot(normal, lightDir), 0.0);
    vec3 diffuse = diff * lightColor;

    // Эффект Френеля (Светящиеся грани под углом, используем цвет сетки для стилистики)
    float fresnel = pow(1.0 - max(dot(normal, viewDir), 0.0), 3.0);
    vec3 rimLight = fresnel * wireframe_color * 0.8; 

    // Сборка цвета
    vec3 result = (ambient + diffuse) * object_color + rimLight;
    
    FragColor = vec4(result, 1.0);
}