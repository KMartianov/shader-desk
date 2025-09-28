// shaders/sphere_vert.glsl
#version 300 es
layout (location = 0) in vec3 aPos;
layout (location = 1) in float aPhase;
layout (location = 2) in vec3 aNormal;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform float time;

// Параметры колебаний
uniform float oscill_amp;      // Амплитуда основных колебаний
uniform float oscill_freq;     // Частота основных колебаний
uniform float wave_amp;        // Амплитуда волновых эффектов
uniform float wave_freq;       // Частота волновых эффектов
uniform float twist_amp;       // Амплитуда скручивания
uniform float pulse_amp;       // Амплитуда пульсации
uniform float noise_amp;       // Амплитуда шума

uniform float sphere_scale;  

out vec3 FragPos;
out vec3 Normal;
out float Phase;

// Шумовая функция для более органичных колебаний
float hash(float n) {
    return fract(sin(n) * 43758.5453);
}

float noise(vec3 x) {
    vec3 p = floor(x);
    vec3 f = fract(x);
    f = f * f * (3.0 - 2.0 * f);
    
    float n = p.x + p.y * 157.0 + 113.0 * p.z;
    
    return mix(mix(mix(hash(n + 0.0), hash(n + 1.0), f.x),
                   mix(hash(n + 157.0), hash(n + 158.0), f.x), f.y),
               mix(mix(hash(n + 113.0), hash(n + 114.0), f.x),
                   mix(hash(n + 270.0), hash(n + 271.0), f.x), f.y), f.z);
}

void main()
{
    // Масштабируем базовую позицию
    vec3 scaledPos = aPos * sphere_scale;
    
    // Базовое колебание на основе фазы и времени
    float base_oscillation = sin(aPhase + time * oscill_freq) * oscill_amp;
    
    // Волновой эффект - зависит от позиции на сфере (используем scaledPos)
    float wave = sin(length(scaledPos) * wave_freq + time * 2.0) * wave_amp;
    
    // Эффект скручивания - зависит от Y-координаты (используем scaledPos)
    float twist = sin(scaledPos.y * 5.0 + time * 1.5) * twist_amp;
    
    // Пульсация всей сферы
    float pulse = sin(time * 0.8) * pulse_amp;
    
    // Шум для органичности (используем scaledPos)
    float noise_val = noise(scaledPos * 3.0 + time) * noise_amp;
    
    // Комбинируем все эффекты
    float total_displacement = base_oscillation + wave + twist + pulse + noise_val;
    
    // Смещаем позицию вдоль нормали
    vec3 displacedPos = scaledPos + aNormal * total_displacement;
    
    FragPos = vec3(model * vec4(displacedPos, 1.0));
    Normal = mat3(transpose(inverse(model))) * aNormal;
    Phase = aPhase;
    gl_Position = projection * view * model * vec4(displacedPos, 1.0);
}
