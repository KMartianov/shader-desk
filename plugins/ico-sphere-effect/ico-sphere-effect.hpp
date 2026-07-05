// src/ico-sphere-effect.hpp
#ifndef ICO_SPHERE_EFFECT_HPP
#define ICO_SPHERE_EFFECT_HPP

#include <GLES3/gl3.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <string>
#include <array>
#include <memory>
#include "wallpaper-effect.hpp"

/**
 * @brief Структура вершины для барицентрического рендеринга.
 * 
 * Включение барицентрических координат (aBary) позволяет фрагментному шейдеру
 * рисовать идеальную проволочную сетку поверх сплошного цвета за ОДИН Draw Call.
 * Это полностью устраняет Z-fighting (мерцание на стыке линий и полигонов).
 */
struct Vertex {
    glm::vec3 position;    // layout (location = 0)
    float phase;           // layout (location = 1) - Случайная фаза для органического шума
    glm::vec3 normal;      // layout (location = 2)
    glm::vec3 barycentric; // layout (location = 3) - (1,0,0), (0,1,0), (0,0,1) для углов треугольника
};

/**
 * @brief Ресурсы для Bloom пост-процессинга (FBO).
 * 
 * Разрешение FBO намеренно уменьшается в 4 раза (width/4, height/4) 
 * по сравнению с разрешением экрана Wayland. Это дает более мягкое, 
 * "широкое" неоновое свечение и снижает нагрузку на GPU ноутбука на 75%.
 */
struct BloomResources {
    GLuint fbo_scene = 0;
    GLuint tex_scene_color = 0;
    GLuint tex_scene_bright = 0; // Текстура для вырезки только ярких (неоновых) участков
    GLuint rbo_depth = 0;

    // Ping-Pong буферы для двухпроходного размытия Гаусса (Gaussian Blur)
    GLuint fbo_pingpong[2] = {0, 0};
    GLuint tex_pingpong[2] = {0, 0};

    uint32_t width = 0;
    uint32_t height = 0;
    bool initialized = false;
};

/**
 * @brief Высокопроизводительный визуальный плагин для Wayland композиторов.
 * Поддерживает аудио-реактивность, физику мыши, MatCap, Bloom и инстансинг.
 */
class IcoSphereEffect : public WallpaperEffect {
public:
    IcoSphereEffect();
    ~IcoSphereEffect() override;

    // --- Реализация интерфейса WallpaperEffect ---
    bool initialize(ICoreContext* core, uint32_t width, uint32_t height) override;
    void render(uint32_t width, uint32_t height) override;
    void cleanup() override;
    
    // ========================================================================
    // --- API НАСТРОЕК (Lua Configuration Methods) ---
    // ========================================================================
    
    // Базовые визуальные параметры
    void set_wireframe_mode(bool enabled) { wireframe_mode = enabled; }
    void set_subdivisions(int value);
    void set_background_color(const glm::vec3& color) { background_color = color; }
    void set_wireframe_color(const glm::vec3& color) { wireframe_color = color; }
    void set_object_color(const glm::vec3& color) { object_color = color; }

    // Деформации и процедурная органика
    void set_oscill_amp(float value) { oscill_amp = value; update_effect_scaling(); }
    void set_oscill_freq(float value) { oscill_freq = value; }
    void set_wave_amp(float value) { wave_amp = value; update_effect_scaling(); }
    void set_wave_freq(float value) { wave_freq = value; }
    void set_twist_amp(float value) { twist_amp = value; update_effect_scaling(); }
    void set_pulse_amp(float value) { pulse_amp = value; update_effect_scaling(); }
    void set_noise_amp(float value) { noise_amp = value; update_effect_scaling(); }

    // Физика вращения (Гроскопический эффект)
    void set_constant_rotation_speed(float value) { constant_rotation_speed = value; }
    void set_rotation_decay(float value) { rotation_decay = value; }
    void set_min_rotation_speed(float value) { min_rotation_speed = value; }
    void set_max_rotation_speed(float value) { max_rotation_speed = value; }

    // Масштабирование
    void set_sphere_scale(float scale);
    float get_sphere_scale() const { return sphere_scale; }

    // --- НОВЫЕ ПАРАМЕТРЫ (Новые фичи движка) ---

    // 1. Bloom Пост-процессинг
    void set_bloom_intensity(float intensity) { bloom_intensity = std::max(0.0f, intensity); }
    
    // 2. MatCap Текстурирование (Хром, Золото, Жемчуг)
    void set_matcap_texture(const std::string& filename);

    // 3. Инстансинг ("Ядро в клетке")
    void set_use_instancing(bool enabled) { use_instancing = enabled; }
    void set_inner_scale(float scale) { inner_scale = scale; }
    void set_outer_scale(float scale) { outer_scale = scale; }

    // 4. Физические ударные волны (Shockwaves)
    // Вызывается из Lua при резком скачке баса для запуска круговой ряби по сфере
    void trigger_shockwave(const glm::vec3& hit_point);

protected:
    ICoreContext* m_core = nullptr;
    void update_effect_scaling();

    // --- Управление шейдерами ---
    std::string active_shader = "default";
    bool needs_shader_reload = true;
    bool reload_shader_program();
    void fetch_uniform_locations();

    // --- Внутренние методы рендеринга и физики ---
    void generate_icosphere(int subdivisions);
    void update_buffers();
    void update_rotation(float dt);
    void update_shockwaves(float dt);

    // --- Методы управления Bloom FBO и MatCap ---
    bool init_bloom_resources(uint32_t width, uint32_t height);
    void destroy_bloom_resources();
    void render_bloom_postprocess(uint32_t width, uint32_t height);
    void load_matcap_from_file(const std::string& path);

    // ========================================================================
    // --- ПЕРЕМЕННЫЕ СОСТОЯНИЯ И ОБЪЕКТЫ OPENGL ---
    // ========================================================================

    // Основная шейдерная программа (Сфера)
    GLuint program = 0;
    GLuint vao = 0, vbo = 0, ebo = 0;
    // Примечание: line_ebo удален, так как барицентрическая сетка рисуется через ebo (GL_TRIANGLES)

    // Шейдерные программы для Bloom (Размытие и Композиция)
    GLuint bloom_blur_program = 0;
    GLuint bloom_final_program = 0;
    BloomResources bloom;
    float bloom_intensity = 0.5f;

    // MatCap текстурирование
    GLuint matcap_texture_id = 0;
    std::string matcap_filename = "";
    bool use_matcap = false;

    // Инстансинг
    bool use_instancing = false;
    float inner_scale = 0.8f;
    float outer_scale = 1.2f;

    // Физика вращения
    glm::quat orientation;
    glm::vec3 angular_velocity;
    float rotation_decay = 0.95f;
    float constant_rotation_speed = 0.1f;
    float min_rotation_speed = 0.001f;
    float max_rotation_speed = 5.0f;

    // Ударные волны (Shockwaves)
    // xyz = координата удара на сфере, w = время с момента удара (сек)
    std::array<glm::vec4, 4> shockwaves;
    uint32_t current_shockwave_idx = 0;

    // --- Указатели на BlackBoard (Шина данных с нулевой задержкой) ---
    float* p_accum_x = nullptr;
    float* p_accum_y = nullptr;
    float last_mouse_x = 0.0f;
    float last_mouse_y = 0.0f;

    float* p_audio_bass = nullptr;
    float* p_audio_mid = nullptr;
    float* p_audio_treble = nullptr;
    float* p_audio_bands = nullptr; // Массив из 64 частот эквалайзера

    // --- Параметры конфигурации сферы ---
    int subdivisions = 3;
    bool needs_regeneration = false;
    float time = 0.0f;
    bool wireframe_mode = true;
    float sphere_scale = 1.0f;

    float oscill_amp = 0.0f, oscill_freq = 1.0f;
    float wave_amp = 0.0f, wave_freq = 1.0f;
    float twist_amp = 0.0f, pulse_amp = 0.0f, noise_amp = 0.0f;
    
    // Масштабированные амплитуды (для сохранения формы при изменении sphere_scale)
    float scaled_oscill_amp, scaled_wave_amp;
    float scaled_twist_amp, scaled_pulse_amp, scaled_noise_amp;

    glm::vec3 background_color = {0.1137f, 0.1137f, 0.1255f};
    glm::vec3 wireframe_color  = {0.5f, 0.5f, 0.7f};
    glm::vec3 object_color     = {0.08f, 0.12f, 0.20f};

    // ========================================================================
    // --- UNIFORM ЛОКАЦИИ (Кэшированные адреса в шейдере) ---
    // ========================================================================
    
    // Стандартные матрицы и освещение
    GLuint u_model = 0, u_view = 0, u_projection = 0, u_time = 0;
    GLuint u_lightColor = 0, u_lightPos = 0, u_viewPos = 0;
    
    // Цвета и режимы
    GLuint u_wireframe_color = 0, u_object_color = 0, u_is_wireframe_pass = 0;
    
    // Деформации
    GLuint u_oscill_amp = 0, u_oscill_freq = 0, u_wave_amp = 0, u_wave_freq = 0;
    GLuint u_twist_amp = 0, u_pulse_amp = 0, u_noise_amp = 0, u_sphere_scale = 0;
    
    // Аудио и Ударные волны
    GLuint u_audio_bass = 0, u_audio_mid = 0, u_audio_treble = 0, u_audio_bands = 0;
    GLuint u_shockwaves = 0;

    // Новые фичи
    GLuint u_use_matcap = 0, u_matcap_tex = 0;
    GLuint u_use_instancing = 0, u_inner_scale = 0, u_outer_scale = 0;

    // Буферы данных для CPU -> GPU
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
};

#endif // ICO_SPHERE_EFFECT_HPP