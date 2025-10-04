#version 300 es
layout (location = 0) in vec3 aPos;
layout (location = 1) in float aPhase;
layout (location = 2) in vec3 aNormal;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform float time;

// Параметры колебаний из конфига
uniform float oscill_amp;
uniform float oscill_freq;
uniform float wave_amp;
uniform float wave_freq;
uniform float twist_amp;
uniform float pulse_amp;
uniform float noise_amp;
uniform float sphere_scale;

// ++ НОВОЕ: Uniform-переменные для аудиоданных ++
uniform float audio_bass;   // Уровень басов (0.0-1.0)
uniform float audio_mid;    // Уровень средних частот (0.0-1.0)
uniform float audio_treble; // Уровень высоких частот (0.0-1.0)

out vec3 FragPos;
out vec3 Normal;
out float Phase;

// Шумовая функция (без изменений)
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
    vec3 scaledPos = aPos * sphere_scale;
    
    // --- Комбинируем базовую анимацию с аудиоданными ---

    // 1. Пульсация: базовая амплитуда + мощный толчок от басов
    float pulse_effect = sin(time * 0.8) * (pulse_amp + audio_bass * 2.0);

    // 2. Волновой эффект: базовые волны + рябь от средних частот
    float wave_effect = sin(length(scaledPos) * wave_freq + time * 2.0) * (wave_amp + audio_mid * 0.5);

    // 3. Шум: базовый шум + высокочастотная "дрожь" от высоких частот
    float noise_effect = noise(scaledPos * 3.0 + time * 2.0) * (noise_amp + audio_treble * 0.8);
    
    // 4. Остальные эффекты остаются без изменений
    float base_oscillation = sin(aPhase + time * oscill_freq) * oscill_amp;
    float twist = sin(scaledPos.y * 5.0 + time * 1.5) * twist_amp;

    // Суммируем все смещения
    float total_displacement = base_oscillation + wave_effect + twist + pulse_effect + noise_effect;
    
    // Применяем смещение к вершине
    vec3 displacedPos = scaledPos + aNormal * total_displacement;
    
    // Стандартные преобразования для вывода
    FragPos = vec3(model * vec4(displacedPos, 1.0));
    Normal = mat3(transpose(inverse(model))) * aNormal;
    Phase = aPhase;
    gl_Position = projection * view * model * vec4(displacedPos, 1.0);
}