#version 300 es
// Обязательно высокая точность для вычислений сферической тригонометрии
precision highp float;

// --- Входные атрибуты вершины ---
layout (location = 0) in vec3 aPos;
layout (location = 1) in float aPhase; 
layout (location = 2) in vec3 aNormal; 
layout (location = 3) in vec3 aBary;


// --- Стандартные матрицы ---
uniform mat4 model;      
uniform mat4 view;       
uniform mat4 projection; 
uniform float time;

// --- Параметры эффектов из файла конфигурации (СТРОГО ОРИГИНАЛЬНЫЕ!) ---
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
uniform float audio_bands[64];

out vec3 FragPos; 
out vec3 vBary;

// ============================================================================
// 1. БИБЛИОТЕКА СФЕРИЧЕСКОЙ ФИЗИКИ
// ============================================================================

// Преобразование декартовых координат в сферические
vec2 get_spherical(vec3 p) {
    float r = length(p);
    // clamp обязателен, чтобы acos не выдал NaN из-за погрешностей float
    float theta = acos(clamp(p.y / r, -1.0, 1.0)); // Широта: [0, PI]
    float phi = atan(p.z, p.x);                    // Долгота: [-PI, PI]
    return vec2(theta, phi);
}

// Динамическая сферическая гармоника l=3, m=2 (Тетраэдрическая симметрия)
float sh_dynamic_3_2(vec2 sph, float phase_shift) {
    float sin_t = sin(sph.x);
    // 15.0 - математический коэффициент для данной формы
    return 15.0 * sin_t * sin_t * cos(sph.x) * cos(2.0 * sph.y + phase_shift);
}

// Динамическая сферическая гармоника l=4, m=3 (Октаэдрическая симметрия)
float sh_dynamic_4_3(vec2 sph, float phase_shift) {
    float sin_t = sin(sph.x);
    return 105.0 * sin_t * sin_t * sin_t * cos(sph.x) * cos(3.0 * sph.y + phase_shift);
}

// ============================================================================
// 2. БЕГУЩИЕ ВОЛНЫ (Геодезическая рябь)
// ============================================================================
// Вычисляет затухающую синусоиду, бегущую по кратчайшей дуге от точки center
float geodesic_ripple(vec3 p, vec3 center, float freq, float speed) {
    float dist = acos(clamp(dot(p, center), -1.0, 1.0));
    // exp(-dist * 1.5) - фактор естественного затухания волны
    return exp(-dist * 1.5) * sin(dist * freq - time * speed);
}

// ============================================================================
// 3. 3D SIMPLEX NOISE
// ============================================================================
vec4 permute(vec4 x){return mod(((x*34.0)+1.0)*x, 289.0);}
vec4 taylorInvSqrt(vec4 r){return 1.79284291400159 - 0.85373472095314 * r;}
float snoise(vec3 v){ 
  const vec2  C = vec2(1.0/6.0, 1.0/3.0);
  const vec4  D = vec4(0.0, 0.5, 1.0, 2.0);
  vec3 i  = floor(v + dot(v, C.yyy));
  vec3 x0 = v - i + dot(i, C.xxx);
  vec3 g = step(x0.yzx, x0.xyz);
  vec3 l = 1.0 - g;
  vec3 i1 = min( g.xyz, l.zxy );
  vec3 i2 = max( g.xyz, l.zxy );
  vec3 x1 = x0 - i1 + 1.0 * C.xxx;
  vec3 x2 = x0 - i2 + 2.0 * C.xxx;
  vec3 x3 = x0 - 1.0 + 3.0 * C.xxx;
  i = mod(i, 289.0); 
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
    vec3 scaledPos = aPos * sphere_scale;
    vec3 normPos = normalize(aPos); 
    
    // Внедряем параметр twist_amp в расчет сферических координат.
    // Это создает закручивание полюсов вокруг Y в зависимости от настройки в Lua.
    vec3 twistedPos = normPos;
    float twist_angle = normPos.y * twist_amp * sin(time * 0.5);
    float cos_t = cos(twist_angle);
    float sin_t = sin(twist_angle);
    twistedPos.x = normPos.x * cos_t - normPos.z * sin_t;
    twistedPos.z = normPos.x * sin_t + normPos.z * cos_t;
    
    vec2 sph = get_spherical(twistedPos);

    float total_disp = 0.0;

    // ========================================================================
    // 1. РЕЗОНАНС (Сферические гармоники)
    // ========================================================================
    // Используем oscill_freq для управления скоростью вращения форм, 
    // а oscill_amp работает как множитель-усилитель от пользователя.
    float rot_phase = time * max(oscill_freq, 0.5); 
    
    float bass_resonance = audio_bass * sh_dynamic_3_2(sph, rot_phase * 2.0);
    float mid_resonance  = audio_mid  * sh_dynamic_4_3(sph, -rot_phase * 3.0);
    
    // Сплющивание полюсов для тяжелого баса (сферическая гармоника l=2, m=0)
    float pole_squash = audio_bass * 0.5 * (3.0 * twistedPos.y * twistedPos.y - 1.0);
    
    // Базовый коэффициент 0.15 + пользовательский oscill_amp
    float res_multiplier = 0.15 + oscill_amp;
    total_disp += (bass_resonance + mid_resonance - pole_squash) * res_multiplier;

    // ========================================================================
    // 2. БЕГУЩИЕ ВОЛНЫ (Рябь от ударов)
    // ========================================================================
    // Используем wave_freq для плотности волн, wave_amp как усилитель
    vec3 north_pole = vec3(0.0, 1.0, 0.0);
    vec3 south_pole = vec3(0.0, -1.0, 0.0);
    
    // Базовая частота ряби = 15.0. Если wave_freq = 0, рябь стандартная.
    float actual_wave_freq = 15.0 + wave_freq;
    
    float ripple_N = geodesic_ripple(twistedPos, north_pole, actual_wave_freq, 8.0);
    float ripple_S = geodesic_ripple(twistedPos, south_pole, actual_wave_freq, 8.0);
    
    // Базовый коэффициент 0.2 + пользовательский wave_amp
    total_disp += audio_bass * (ripple_N + ripple_S) * (0.2 + wave_amp);

    // ========================================================================
    // 3. ОРГАНИЧНЫЙ СПЕКТРАЛЬНЫЙ МАППИНГ (64 Частоты)
    // ========================================================================
    // 3D-шум используется как координата, чтобы плавно и хаотично 
    // "натянуть" 64 полосы эквалайзера на поверхность сферы.
    float spatial_noise = snoise(twistedPos * 1.5 + time * 0.2); // [-1..1]
    float normalized_noise = (spatial_noise + 1.0) * 0.5;        // [0..1]
    
    float exact_idx = normalized_noise * 63.0;
    int idx1 = int(exact_idx);
    int idx2 = min(idx1 + 1, 63);
    float blend = fract(exact_idx); 
    
    // Точная энергия аудио-полосы для данной точки на сфере
    float band_energy = mix(audio_bands[idx1], audio_bands[idx2], blend);
    
    // Создаем микро-рельеф. noise_amp из Lua усиливает влияние спектра.
    float micro_surface = sin(sph.x * 20.0) * cos(sph.y * 20.0);
    total_disp += band_energy * micro_surface * (0.4 + noise_amp);

    // ========================================================================
    // 4. ТРЕБЛ И ПУЛЬС
    // ========================================================================
    // Мелкий высокочастотный шум
    total_disp += audio_treble * snoise(twistedPos * 8.0 - time * 5.0) * 0.15;
    
    // Пользовательский pulse_amp добавляет простое "дыхание" всей сферы
    total_disp += sin(time * max(oscill_freq, 1.0)) * pulse_amp;

    // ========================================================================
    // ПРИМЕНЕНИЕ И ВЫВОД
    // ========================================================================
    // Защита геометрии (чтобы вершины не пересекли центр сферы)
    total_disp = clamp(total_disp, -0.7, 1.5);

    // Выталкиваем вершину вдоль нормали
    vec3 displacedPos = scaledPos + normPos * total_disp;
    
    vBary = aBary;

    FragPos = vec3(model * vec4(displacedPos, 1.0));
    gl_Position = projection * view * vec4(FragPos, 1.0);
}