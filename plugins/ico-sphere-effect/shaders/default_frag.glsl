#version 300 es
// Обязательно высокая точность для вычисления нормалей
precision highp float; 

in vec3 FragPos;
in vec3 vBary;  

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
    // Расстояние от камеры до текущего пикселя
    float distance = length(viewPos - FragPos);

    // --- ГЛАВНЫЙ РЕЖИМ: Проволочная сетка (Wireframe) ---
    // ========================================================================
    // ГЛАВНЫЙ РЕЖИМ: Проволочная сетка (Barycentric Wireframe)
    // ========================================================================
    if (is_wireframe_pass) {
        // 1. Находим расстояние от текущего пикселя до ближайшего края треугольника
        float minBary = min(min(vBary.x, vBary.y), vBary.z);
        
        // 2. Вычисляем толщину линии в пикселях. 
        // Функция fwidth гарантирует, что линия будет ровно 1.2 пикселя 
        // независимо от того, близко сфера к камере или далеко!
        float edgeWidth = fwidth(minBary) * 10.2; 
        
        // 3. КРИТИЧЕСКИЙ ШАГ: Если пиксель находится внутри полигона 
        // (дальше от края, чем толщина линии) — полностью выбрасываем его!
        if (minBary > edgeWidth) {
            discard;
        }

        // 4. Отрисовываем саму линию сетки с голографическим затуханием в глубину
        float fogFactor = clamp(1.0 - (distance - 2.0) / 2.5, 0.2, 1.0);
        vec3 finalWireColor = wireframe_color * fogFactor;
        
        FragColor = vec4(finalWireColor, 1.0);
        return;
    }

    // --- РЕЗЕРВНЫЙ РЕЖИМ: Сплошная сфера (Solid Mode) ---
    // Включается, если wireframe_mode = false. Выглядит как Low-Poly кристалл.

    // Аппаратное вычисление нормалей треугольника (Flat Shading)
    vec3 dx = dFdx(FragPos);
    vec3 dy = dFdy(FragPos);
    vec3 normal = normalize(cross(dx, dy));

    vec3 viewDir = normalize(viewPos - FragPos);
    vec3 lightDir = normalize(lightPos - FragPos);

    // Базовое освещение (Ambient + Diffuse)
    vec3 ambient = 0.2 * lightColor;
    float diff = max(dot(normal, lightDir), 0.0);
    vec3 diffuse = diff * lightColor;

    // Легкий эффект Френеля (подсвечивает края полигонов цветом сетки)
    float fresnel = pow(1.0 - max(dot(normal, viewDir), 0.0), 3.0);
    vec3 rimLight = fresnel * wireframe_color * 0.6; 

    vec3 result = (ambient + diffuse) * object_color + rimLight;
    
    FragColor = vec4(result, 1.0);
}