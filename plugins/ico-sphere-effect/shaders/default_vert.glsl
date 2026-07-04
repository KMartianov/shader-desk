// Указываем версию GLSL ES (OpenGL for Embedded Systems)
#version 300 es

// --- Входные атрибуты вершины ---
layout (location = 0) in vec3 aPos;
layout (location = 1) in float aPhase; // Оставлено для совместимости с C++
layout (location = 2) in vec3 aNormal; // Оставлено для совместимости с C++
layout (location = 3) in vec3 aBary;

// --- Стандартные матрицы ---
uniform mat4 model;      
uniform mat4 view;       
uniform mat4 projection; 

// Переменная времени для анимации
uniform float time;

// --- Параметры эффектов из файла конфигурации (Не удалять комментарии!) ---
// @param oscill_amp | float | 0.0 | Амплитуда базовых колебаний.
uniform float oscill_amp;
// @param oscill_freq | float | 1.0 | Частота базовых колебаний.
uniform float oscill_freq;
// @param wave_amp | float | 0.0 | Амплитуда волнового эффекта.
uniform float wave_amp;
// @param wave_freq | float | 10.0 | Частота (плотность) волн на поверхности.
uniform float wave_freq;
// @param twist_amp | float | 0.0 | Сила эффекта скручивания.
uniform float twist_amp;
// @param pulse_amp | float | 0.0 | Амплитуда базовой пульсации.
uniform float pulse_amp;
// @param noise_amp | float | 0.0 | Амплитуда шумовой деформации.
uniform float noise_amp;
// @param sphere_scale | float | 1.0 | Общий масштаб сферы.
uniform float sphere_scale;

// --- Uniform-переменные для реакции на аудио ---
uniform float audio_bass;
uniform float audio_mid;
uniform float audio_treble;
uniform float audio_bands[64]; // Массив всех 64 частот

out vec3 FragPos; 
out vec3 vBary;

// --- 3D Simplex Noise (Отличный органический шум) ---
vec4 permute(vec4 x){return mod(((x*34.0)+1.0)*x, 289.0);}
vec4 taylorInvSqrt(vec4 r){return 1.79284291400159 - 0.85373472095314 * r;}
float snoise(vec3 v){ 
  const vec2  C = vec2(1.0/6.0, 1.0/3.0) ;
  const vec4  D = vec4(0.0, 0.5, 1.0, 2.0);
  vec3 i  = floor(v + dot(v, C.yyy) );
  vec3 x0 = v - i + dot(i, C.xxx) ;
  vec3 g = step(x0.yzx, x0.xyz);
  vec3 l = 1.0 - g;
  vec3 i1 = min( g.xyz, l.zxy );
  vec3 i2 = max( g.xyz, l.zxy );
  vec3 x1 = x0 - i1 + 1.0 * C.xxx;
  vec3 x2 = x0 - i2 + 2.0 * C.xxx;
  vec3 x3 = x0 - 1.0 + 3.0 * C.xxx;
  i = mod(i, 289.0 ); 
  vec4 p = permute( permute( permute( 
             i.z + vec4(0.0, i1.z, i2.z, 1.0 ))
           + i.y + vec4(0.0, i1.y, i2.y, 1.0 )) 
           + i.x + vec4(0.0, i1.x, i2.x, 1.0 ));
  float n_ = 1.0/7.0; 
  vec3  ns = n_ * D.wyz - D.xzx;
  vec4 j = p - 49.0 * floor(p * ns.z *ns.z);
  vec4 x_ = floor(j * ns.z);
  vec4 y_ = floor(j - 7.0 * x_ );
  vec4 x = x_ *ns.x + ns.yyyy;
  vec4 y = y_ *ns.x + ns.yyyy;
  vec4 h = 1.0 - abs(x) - abs(y);
  vec4 b0 = vec4( x.xy, y.xy );
  vec4 b1 = vec4( x.zw, y.zw );
  vec4 s0 = floor(b0)*2.0 + 1.0;
  vec4 s1 = floor(b1)*2.0 + 1.0;
  vec4 sh = -step(h, vec4(0.0));
  vec4 a0 = b0.xzyw + s0.xzyw*sh.xxyy ;
  vec4 a1 = b1.xzyw + s1.xzyw*sh.zzww ;
  vec3 p0 = vec3(a0.xy,h.x);
  vec3 p1 = vec3(a0.zw,h.y);
  vec3 p2 = vec3(a1.xy,h.z);
  vec3 p3 = vec3(a1.zw,h.w);
  vec4 norm = taylorInvSqrt(vec4(dot(p0,p0), dot(p1,p1), dot(p2, p2), dot(p3,p3)));
  p0 *= norm.x; p1 *= norm.y; p2 *= norm.z; p3 *= norm.w;
  vec4 m = max(0.6 - vec4(dot(x0,x0), dot(x1,x1), dot(x2,x2), dot(x3,x3)), 0.0);
  m = m * m;
  return 42.0 * dot( m*m, vec4( dot(p0,x0), dot(p1,x1), dot(p2,x2), dot(p3,x3) ) );
}

void main()
{
    // Масштабируем сферу, но для вычислений берем идеальную сферу (нормализованный вектор)
    vec3 scaledPos = aPos * sphere_scale;
    vec3 normPos = normalize(aPos); 
    
    // --- 1. ОРГАНИЧЕСКИЙ ЭКВАЛАЙЗЕР ---
    // Модуль Y: 0.0 на экваторе, 1.0 на полюсах
    float mappedY = abs(normPos.y); 
    
    // Искривляем само чтение частот! Это разбивает строгие кольца на красивые "островки"
    float bandWarp = snoise(normPos * 3.0 - time * 0.2) * 0.15;
    
    // Вычисляем индекс от 0 до 63
    int bandIndex = int(clamp((mappedY + bandWarp) * 63.0, 0.0, 63.0));
    
    // Достаем частоту. pow() делает "шипы" визуально острее и ритмичнее
    float local_freq = audio_bands[bandIndex];
    float spike_disp = pow(local_freq, 1.5) * 0.8;

    // --- 2. БАЗОВАЯ ОРГАНИКА (ШУМ) ---
    // Глубокий, плавный бас изгибает саму форму сферы (как капля воды)
    float bass_warp = snoise(normPos * 1.2 + time * 0.8) * (audio_bass * 0.4);
    
    // Легкое дыхание в тишине, чтобы сфера казалась живой
    float idle_morph = snoise(normPos * 2.5 - time * 0.1) * 0.08;

    // --- 3. НАСТРОЙКИ ПОЛЬЗОВАТЕЛЯ ИЗ LUA ---
    // Используем normPos.y, чтобы рисунок волн не ломался при изменении sphere_scale
    float config_wave = sin(normPos.y * wave_freq + time * 2.0) * wave_amp;
    float config_noise = snoise(normPos * 4.0 + time) * noise_amp;
    float config_twist = sin(normPos.y * 8.0 + time * 1.5) * twist_amp;
    float config_pulse = sin(time * oscill_freq) * pulse_amp;

    // --- СУММИРУЕМ ВСЕ ДЕФОРМАЦИИ ---
    float total_disp = idle_morph + bass_warp + spike_disp + 
                       config_wave + config_noise + config_twist + config_pulse;
    
    // Сдвигаем вершину от центра (normPos совпадает с нормалью для сферы в 0,0,0)
    vec3 displacedPos = scaledPos + normPos * total_disp;
    vBary = aBary;

    FragPos = vec3(model * vec4(displacedPos, 1.0));
    gl_Position = projection * view * vec4(FragPos, 1.0);
}