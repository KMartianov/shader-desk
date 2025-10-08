// Указываем версию GLSL ES (OpenGL for Embedded Systems), 
// которая используется в WebGL 2 и мобильных устройствах.
#version 300 es

// --- Входные атрибуты вершины ---
// Эти данные считываются для каждой вершины из буферов в C++.
// layout (location = ...) связывает атрибут с определенным индексом.

// (location = 0): Позиция вершины в локальных координатах модели.
layout (location = 0) in vec3 aPos;
// (location = 1): Уникальное значение фазы для каждой вершины, 
// используется для создания индивидуальной анимации колебаний.
layout (location = 1) in float aPhase;
// (location = 2): Вектор нормали для вершины. Он указывает "наружу" от поверхности 
// и используется для определения направления смещения.
layout (location = 2) in vec3 aNormal;


// --- Uniform-переменные ---
// Глобальные переменные, которые передаются из C++ кода и одинаковы для всех вершин.

// Стандартные матрицы для преобразования координат.
uniform mat4 model;      // Из локальных координат в мировые.
uniform mat4 view;       // Из мировых координат в координаты камеры.
uniform mat4 projection; // Из координат камеры в проекцию на экран.

// Переменная времени для анимации.
uniform float time;

// --- Параметры эффектов из файла конфигурации ---
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
// Эти значения (от 0.0 до 1.0) передаются из аудиоанализатора.
// @param audio_bass | float | 0.0 | Текущий уровень басов.
uniform float audio_bass;
// @param audio_mid | float | 0.0 | Текущий уровень средних частот.
uniform float audio_mid;
// @param audio_treble | float | 0.0 | Текущий уровень высоких частот.
uniform float audio_treble;


// --- Выходные переменные (передаются в фрагментный шейдер) ---
out vec3 FragPos; // Позиция вершины в мировых координатах для расчетов освещения.
out vec3 Normal;  // Трансформированная нормаль для расчетов освещения.
out float Phase;   // Передаем фазу дальше, если она понадобится.


// --- Функции процедурного шума ---

// Простая хеш-функция для генерации псевдослучайных чисел.
float hash(float n) {
    return fract(sin(n) * 43758.5453);
}

// 3D-шум, создающий плавные, естественные случайные узоры.
// Он использует интерполяцию между значениями хеш-функции.
float noise(vec3 x) {
    vec3 p = floor(x);
    vec3 f = fract(x);
    f = f * f * (3.0 - 2.0 * f); // Сглаживание для интерполяции
    float n = p.x + p.y * 157.0 + 113.0 * p.z;
    return mix(mix(mix(hash(n + 0.0), hash(n + 1.0), f.x),
                   mix(hash(n + 157.0), hash(n + 158.0), f.x), f.y),
               mix(mix(hash(n + 113.0), hash(n + 114.0), f.x),
                   mix(hash(n + 270.0), hash(n + 271.0), f.x), f.y), f.z);
}


// --- Главная функция шейдера, выполняется для каждой вершины ---
void main()
{
    // Применяем начальный масштаб к позиции вершины.
    vec3 scaledPos = aPos * sphere_scale;
    
    // --- Расчет каждого эффекта деформации ---
    // Мы комбинируем базовую амплитуду из конфига с влиянием аудиоданных.

    // 1. Эффект Пульсации: создает общее расширение и сжатие сферы.
    // Усиливается басами для создания "ударов".
    float pulse_effect = sin(time * 0.8) * (pulse_amp + audio_bass * 2.0);

    // 2. Волновой Эффект: создает концентрические волны на поверхности.
    // Средние частоты добавляют мелкую рябь к основным волнам.
    float wave_effect = sin(length(scaledPos) * wave_freq + time * 2.0) * (wave_amp + audio_mid * 0.5);

    // 3. Эффект Шума: добавляет хаотичную, органическую деформацию.
    // Высокие частоты создают высокочастотную "дрожь" поверхности.
    float noise_effect = noise(scaledPos * 3.0 + time * 0.5) * (noise_amp + audio_treble * 0.8);
    
    // 4. Остальные эффекты, не зависящие от аудио.
    // Базовые колебания: каждая вершина движется по синусоиде со своей фазой.
    float base_oscillation = sin(aPhase + time * oscill_freq) * oscill_amp;
    // Скручивание: смещает вершины по горизонтали в зависимости от их высоты (Y).
    float twist = sin(scaledPos.y * 5.0 + time * 1.5) * twist_amp;

    // --- Суммируем все смещения в одно значение ---
    float total_displacement = base_oscillation + wave_effect + twist + pulse_effect + noise_effect;
    
    // --- Применяем итоговое смещение ---
    // Смещаем позицию вершины вдоль ее нормали на рассчитанную величину.
    vec3 displacedPos = scaledPos + aNormal * total_displacement;
    
    // --- Финальные преобразования и вывод ---
    
    // Преобразуем смещенную позицию в мировые координаты и передаем в фрагментный шейдер.
    FragPos = vec3(model * vec4(displacedPos, 1.0));
    
    // Корректно трансформируем нормаль для освещения (важно при неоднородном масштабировании).
    Normal = mat3(transpose(inverse(model))) * aNormal;
    
    // Просто передаем фазу дальше.
    Phase = aPhase;
    
    // Обязательный вывод: вычисляем финальную позицию вершины на экране.
    gl_Position = projection * view * model * vec4(displacedPos, 1.0);
}