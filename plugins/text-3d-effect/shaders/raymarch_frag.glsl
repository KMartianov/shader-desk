// src/plugins/text-3d-effect/shaders/raymarch_frag.glsl
#version 300 es
precision highp float;

out vec4 FragColor;

uniform vec2 u_resolution;
uniform float u_time;
uniform mat4 u_inv_model; 
uniform float u_extrusion;
uniform float u_bend_radius;
uniform vec3 u_text_color;
uniform vec3 u_bg_color;
uniform float u_text_aspect;
uniform float u_sdf_multiplier;

uniform sampler2D u_sdf_tex;

// Искривление пространства (разворачиваем цилиндр в плоскость)
vec3 opUnBend(vec3 p) {
    if (u_bend_radius <= 0.001) return p; 
    float theta = atan(p.x, p.z + u_bend_radius);
    float r = length(vec2(p.x, p.z + u_bend_radius));
    return vec3(theta * u_bend_radius, p.y, r - u_bend_radius);
}

// 1. ЧИСТАЯ ФУНКЦИЯ ДИСТАНЦИИ (Только геометрия, без искажений Липшица)
float map(vec3 local_p) {
    local_p = opUnBend(local_p);

    vec2 box_size = vec2(u_text_aspect, 1.0);
    vec2 uv = local_p.xy * vec2(0.5 / u_text_aspect, 0.5) + 0.5;
    
    // ИСПОЛЬЗУЕМ textureLod! Это убирает артефакты нормалей из-за разности мипмапов
    float raw_sdf = textureLod(u_sdf_tex, clamp(vec2(uv.x, 1.0 - uv.y), 0.0, 1.0), 0.0).r;
    float d2d = (0.5 - raw_sdf) * 2.0 * u_sdf_multiplier; 
    
    // Ограничиваем текстуру рамкой, чтобы избежать "размазывания" крайних пикселей
    vec2 d_box = abs(local_p.xy) - box_size;
    float box_dist = length(max(d_box, 0.0)) + min(max(d_box.x, d_box.y), 0.0);
    d2d = max(d2d, box_dist);

    // Экструзия в 3D
    vec2 w = vec2(d2d, abs(local_p.z) - u_extrusion);
    return min(max(w.x, w.y), 0.0) + length(max(w, 0.0));
}

// 2. ВЫЧИСЛЕНИЕ НОРМАЛЕЙ
vec3 calcNormal(vec3 p) {
    const vec2 e = vec2(1.0, -1.0) * 0.0015; // Точность адаптирована под SDF шрифт
    return normalize(
        e.xyy * map(p + e.xyy) + 
        e.yyx * map(p + e.yyx) + 
        e.yxy * map(p + e.yxy) + 
        e.xxx * map(p + e.xxx)
    );
}

// 3. АНАЛИТИЧЕСКИЙ AMBIENT OCCLUSION (Затенение в углах)
float calcAO(vec3 pos, vec3 nor) {
    float occ = 0.0;
    float sca = 1.0;
    for(int i = 0; i < 4; i++) {
        float h = 0.02 + 0.05 * float(i);
        float d = map(pos + h * nor);
        occ += (h - d) * sca;
        sca *= 0.85;
        if(occ > 0.35) break; // Оптимизация раннего выхода
    }
    return clamp(1.0 - 2.5 * occ, 0.0, 1.0);
}

// 4. МЯГКИЕ ТЕНИ (Raymarched Soft Shadows)
float calcShadow(vec3 ro, vec3 rd, float mint, float maxt, float k) {
    float res = 1.0;
    float t = mint;
    for(int i = 0; i < 16; i++) { // Ограничено 16 шагами для экономии GPU
        float h = map(ro + rd * t);
        res = min(res, k * h / t);
        t += clamp(h, 0.02, 0.2);
        if(res < 0.005 || t > maxt) break;
    }
    return clamp(res, 0.0, 1.0);
}

void main() {
    vec2 uv = (gl_FragCoord.xy - 0.5 * u_resolution.xy) / u_resolution.y;
    float camera_z = max(3.0, u_text_aspect * 1.2);

    vec3 ro = vec3(0.0, 0.0, camera_z);
    vec3 rd = normalize(vec3(uv, -1.0));

    vec3 local_ro = (u_inv_model * vec4(ro, 1.0)).xyz;
    vec3 local_rd = normalize((u_inv_model * vec4(rd, 0.0)).xyz);

    // ОПТИМИЗАЦИЯ: Точная ограничивающая сфера
    float bounding_r = (length(vec2(u_text_aspect, 1.0)) + u_extrusion) * (1.0 + u_bend_radius * 0.15);
    float b = dot(local_ro, local_rd);
    float c = dot(local_ro, local_ro) - bounding_r * bounding_r;
    float h = b * b - c;
    
    if (h < 0.0 || (c > 0.0 && b > 0.0)) {
        FragColor = vec4(u_bg_color, 1.0);
        return;
    }

    // 5. ГЛАВНЫЙ ЦИКЛ RAYMARCHING
    float t = max(0.0, -b - sqrt(h)); // Начинаем шагать прямо от границы сферы, а не от камеры!
    const float max_d = 20.0; 
    vec3 p;
    float d = 0.0;
    
    for(int i = 0; i < 80; i++) {
        p = local_ro + local_rd * t; 
        d = map(p);
        
        // Порог точности увеличивается с расстоянием
        float thresh = 0.0005 * (1.0 + t * 0.5); 
        
        if(d < thresh || t > max_d) break;
        
        // ПРАВИЛЬНЫЙ ФИКС ЛИПШИЦА: Масштабируем только шаг, а не само поле SDF!
        float step_scale = 0.95; 
        if (u_bend_radius > 0.001) {
            float r = length(vec2(p.x, p.z + u_bend_radius));
            // Clamp защищает от зависания луча на оси цилиндра
            step_scale *= clamp(r / u_bend_radius, 0.25, 1.0);
        }
        
        t += d * step_scale;
    }

    vec3 color = u_bg_color;

    // Если мы врезались в объект (или пролетели о-о-очень близко для Anti-Aliasing)
    if(t < max_d) {
        vec3 normal = calcNormal(p);
        
        // Вектора освещения
        vec3 view_dir  = normalize(local_ro - p);
        vec3 light_key = normalize(vec3(1.0, 1.5, 2.0));  // Основной свет
        vec3 light_fill = normalize(vec3(-1.0, 0.5, -1.0)); // Заполняющий свет снизу/сзади
        
        // Материалы и освещенность
        float diff_key  = max(dot(normal, light_key), 0.0);
        float diff_fill = max(dot(normal, light_fill), 0.0) * 0.3; // Fill light тусклее
        
        // Тени и AO
        float shadow = calcShadow(p + normal * 0.005, light_key, 0.01, 3.0, 12.0);
        float ao = calcAO(p, normal);
        
        // Блики (Specular GGX approximation)
        vec3 half_vector = normalize(light_key + view_dir);
        float spec_angle = max(dot(normal, half_vector), 0.0);
        float specular = pow(spec_angle, 32.0) * diff_key * shadow;
        
        // Френель (подсветка краев)
        float fresnel = pow(1.0 - max(dot(normal, view_dir), 0.0), 4.0);

        // Композитинг студийного света
        vec3 ambient = u_text_color * 0.15 * ao;
        vec3 diffuse = u_text_color * (diff_key * shadow + diff_fill) * ao;
        
        color = ambient + diffuse;
        color += vec3(1.0) * specular * 0.6;         // Белый блик
        color += u_text_color * fresnel * 0.8 * ao;  // Краевое свечение
        
        // Сглаживание краев (Псевдо-AA без supersampling)
        float smooth_edge = clamp(d / 0.003, 0.0, 1.0);
        color = mix(color, u_bg_color, smooth_edge);
    }

    // ACES Tonemapping + Gamma correction (дает очень сочные и кинематографичные цвета)
    color = clamp((color * (2.51 * color + 0.03)) / (color * (2.43 * color + 0.59) + 0.14), 0.0, 1.0);
    color = pow(color, vec3(1.0/2.2));

    FragColor = vec4(color, 1.0);
}