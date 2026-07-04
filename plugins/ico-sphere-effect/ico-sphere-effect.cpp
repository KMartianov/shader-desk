// src/ico-sphere-effect.cpp
// This file combines effect logic and plugin code to compile into a single .so file.
#define GLM_ENABLE_EXPERIMENTAL

// --- REQUIRED HEADERS ---
#include "ico-sphere-effect.hpp"
#include "wallpaper-effect.hpp"
#include "shader-utils.hpp" 

#include <iostream>
#include <fstream>
#include <sstream>
#include <cmath>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <random>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include <cstddef> // Для offsetof
// Подключаем stb_image для загрузки MatCap текстур (хром, золото)
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// Calculates the midpoint index of an edge for icosphere subdivision
unsigned int get_midpoint_index(unsigned int i1, unsigned int i2,
                               std::vector<glm::vec3>& vertices,
                               std::map<std::pair<unsigned int, unsigned int>, unsigned int>& cache) {
    std::pair<unsigned int, unsigned int> edge_key(std::min(i1, i2), std::max(i1, i2));
    auto it = cache.find(edge_key);
    if (it != cache.end()) {
        return it->second;
    }

    glm::vec3 v1 = vertices[i1];
    glm::vec3 v2 = vertices[i2];
    glm::vec3 midpoint = glm::normalize((v1 + v2) * 0.5f);
    unsigned int new_index = vertices.size();
    vertices.push_back(midpoint);
    cache[edge_key] = new_index;
    return new_index;
}


// --- IcoSphereEffect CLASS IMPLEMENTATION ---

IcoSphereEffect::IcoSphereEffect() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> dis(-1.0f, 1.0f);
    orientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    angular_velocity = glm::vec3(dis(gen), dis(gen), dis(gen)) * 0.1f;
    
    // Visual parameters initialization
    wireframe_mode = true;
    subdivisions = 3;
    sphere_scale = 1.0f;
    oscill_amp = 0.0f;
    oscill_freq = 1.0f;
    wave_amp = 0.0f;
    wave_freq = 1.0f;
    twist_amp = 0.0f;
    pulse_amp = 0.0f;
    noise_amp = 0.0f;
    rotation_decay = 0.98f;
    max_rotation_speed = 3.0f;
    min_rotation_speed = 0.001f;
    constant_rotation_speed = 0.0f;

    update_effect_scaling();
}

IcoSphereEffect::~IcoSphereEffect() {
    cleanup();
}

void IcoSphereEffect::generate_icosphere(int subdivisions_level) {
    vertices.clear();
    indices.clear();
    
    // 1. Генерируем базовый икосаэдр (12 вершин, 20 треугольников)
    const float t = (1.0f + std::sqrt(5.0f)) / 2.0f;
    std::vector<glm::vec3> base_pos = {
        glm::normalize(glm::vec3(-1,  t,  0)), glm::normalize(glm::vec3( 1,  t,  0)),
        glm::normalize(glm::vec3(-1, -t,  0)), glm::normalize(glm::vec3( 1, -t,  0)),
        glm::normalize(glm::vec3( 0, -1,  t)), glm::normalize(glm::vec3( 0,  1,  t)),
        glm::normalize(glm::vec3( 0, -1, -t)), glm::normalize(glm::vec3( 0,  1, -t)),
        glm::normalize(glm::vec3( t,  0, -1)), glm::normalize(glm::vec3( t,  0,  1)),
        glm::normalize(glm::vec3(-t,  0, -1)), glm::normalize(glm::vec3(-t,  0,  1))
    };

    std::vector<unsigned int> base_indices = {
        0, 11, 5,   0, 5, 1,    0, 1, 7,    0, 7, 10,   0, 10, 11,
        1, 5, 9,    5, 11, 4,   11, 10, 2,  10, 7, 6,   7, 1, 8,
        3, 9, 4,    3, 4, 2,    3, 2, 6,    3, 6, 8,    3, 8, 9,
        4, 9, 5,    2, 4, 11,   6, 2, 10,   8, 6, 7,    9, 8, 1
    };

    // 2. Сабдивидение (Подразбиение треугольников)
    for (int i = 0; i < subdivisions_level; i++) {
        std::vector<unsigned int> new_indices;
        std::map<std::pair<unsigned int, unsigned int>, unsigned int> midpoint_cache;

        for (size_t j = 0; j < base_indices.size(); j += 3) {
            unsigned int i1 = base_indices[j], i2 = base_indices[j+1], i3 = base_indices[j+2];
            unsigned int im12 = get_midpoint_index(i1, i2, base_pos, midpoint_cache);
            unsigned int im23 = get_midpoint_index(i2, i3, base_pos, midpoint_cache);
            unsigned int im31 = get_midpoint_index(i3, i1, base_pos, midpoint_cache);
            new_indices.insert(new_indices.end(), {i1, im12, im31, im12, i2, im23, im31, im23, i3, im12, im23, im31});
        }
        base_indices = new_indices;
    }

    // 3. Генерация случайных фаз для процедурного шума
    std::mt19937 gen(0);
    std::uniform_real_distribution<float> dis(0.0f, 2.0f * 3.1415926535f);
    std::vector<float> base_phases(base_pos.size());
    for (size_t i = 0; i < base_pos.size(); i++) base_phases[i] = dis(gen);

    // 4. Расчет нормалей
    std::vector<glm::vec3> base_normals(base_pos.size(), glm::vec3(0.0f));
    for (size_t i = 0; i < base_indices.size(); i += 3) {
        glm::vec3 v1 = base_pos[base_indices[i]], v2 = base_pos[base_indices[i+1]], v3 = base_pos[base_indices[i+2]];
        glm::vec3 normal = glm::normalize(glm::cross(v2 - v1, v3 - v1));
        base_normals[base_indices[i]] += normal; base_normals[base_indices[i+1]] += normal; base_normals[base_indices[i+2]] += normal;
    }
    for (auto& n : base_normals) n = glm::normalize(n);

    // ========================================================================
    // 5. КРИТИЧЕСКИЙ ШАГ: Разворачивание индексов под Барицентрическую сетку.
    // Чтобы каждый угол треугольника знал, что он (1,0,0), (0,1,0) или (0,0,1),
    // мы дублируем вершины для каждого треугольника (Unindexed Mesh).
    // Это полностью убирает Z-fighting при отрисовке сетки поверх полигонов!
    // ========================================================================
    vertices.reserve(base_indices.size());
    indices.reserve(base_indices.size());

    const glm::vec3 bary_coords[3] = {
        glm::vec3(1.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 1.0f)
    };

    for (size_t i = 0; i < base_indices.size(); i++) {
        unsigned int orig_idx = base_indices[i];
        
        Vertex v;
        v.position = base_pos[orig_idx];
        v.phase = base_phases[orig_idx];
        v.normal = base_normals[orig_idx];
        v.barycentric = bary_coords[i % 3]; // Назначаем углы треугольника

        vertices.push_back(v);
        indices.push_back(static_cast<unsigned int>(i)); // Линейный индекс
    }
}


void IcoSphereEffect::update_effect_scaling() {
    scaled_oscill_amp = oscill_amp * sphere_scale;
    scaled_wave_amp = wave_amp * sphere_scale;
    scaled_twist_amp = twist_amp * sphere_scale;
    scaled_pulse_amp = pulse_amp * sphere_scale;
    scaled_noise_amp = noise_amp * sphere_scale;
}

bool IcoSphereEffect::initialize(ICoreContext* core, uint32_t width, uint32_t height) {
    if (program != 0) return true;

    // --- 1. ПРИВЯЗКА ПАМЯТИ ИЗ BLACKBOARD ---
    p_accum_x = core->get_blackboard()->bind_float("mouse.accum_x");
    p_accum_y = core->get_blackboard()->bind_float("mouse.accum_y");
    p_audio_bass = core->get_blackboard()->bind_float("audio.bass");
    p_audio_mid = core->get_blackboard()->bind_float("audio.mid");
    p_audio_treble = core->get_blackboard()->bind_float("audio.treble");
    p_audio_bands = core->get_blackboard()->bind_float_array("audio.bands", 64);

    // --- 2. ЗАГРУЗКА ШЕЙДЕРОВ ---
    if (!reload_shader_program()) {
        return false;
    }

    // --- 3. ГЕНЕРАЦИЯ ГЕОМЕТРИИ (Уже с барицентрическими координатами!) ---
    generate_icosphere(subdivisions);
    
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);
    // ВАЖНО: line_ebo больше нет, так как сетка рисуется фрагментным шейдером через ebo!

    update_buffers();
    return true;
}

void IcoSphereEffect::cleanup() {
    // 1. Удаляем шейдерные программы (включая шейдеры Bloom пост-процессинга)
    if (program) glDeleteProgram(program);
    if (bloom_blur_program) glDeleteProgram(bloom_blur_program);
    if (bloom_final_program) glDeleteProgram(bloom_final_program);
    
    // 2. Удаляем буферы геометрии
    if (vao) glDeleteVertexArrays(1, &vao);
    if (vbo) glDeleteBuffers(1, &vbo);
    if (ebo) glDeleteBuffers(1, &ebo);
    
    // 3. Удаляем MatCap текстуру и FBO ресурсы для Bloom
    if (matcap_texture_id) glDeleteTextures(1, &matcap_texture_id);
    destroy_bloom_resources();

    program = bloom_blur_program = bloom_final_program = 0;
    vao = vbo = ebo = matcap_texture_id = 0;
    std::cout << "[IcoSphere] Effect resources cleaned up successfully." << std::endl;
}


void IcoSphereEffect::fetch_uniform_locations() {
    if (!program) return;
    
    // Стандартные
    u_model = glGetUniformLocation(program, "model");
    u_view = glGetUniformLocation(program, "view");
    u_projection = glGetUniformLocation(program, "projection");
    u_time = glGetUniformLocation(program, "time");
    u_lightColor = glGetUniformLocation(program, "lightColor");
    u_lightPos = glGetUniformLocation(program, "lightPos");
    u_viewPos = glGetUniformLocation(program, "viewPos");

    // Цвета и сетка
    u_wireframe_color = glGetUniformLocation(program, "wireframe_color");
    u_object_color = glGetUniformLocation(program, "object_color");
    u_is_wireframe_pass = glGetUniformLocation(program, "is_wireframe_pass");

    // Деформации
    u_oscill_amp = glGetUniformLocation(program, "oscill_amp");
    u_oscill_freq = glGetUniformLocation(program, "oscill_freq");
    u_wave_amp = glGetUniformLocation(program, "wave_amp");
    u_wave_freq = glGetUniformLocation(program, "wave_freq");
    u_twist_amp = glGetUniformLocation(program, "twist_amp");
    u_pulse_amp = glGetUniformLocation(program, "pulse_amp");
    u_noise_amp = glGetUniformLocation(program, "noise_amp");
    u_sphere_scale = glGetUniformLocation(program, "sphere_scale");

    // Аудио и ударные волны
    u_audio_bass = glGetUniformLocation(program, "audio_bass");
    u_audio_mid = glGetUniformLocation(program, "audio_mid");
    u_audio_treble = glGetUniformLocation(program, "audio_treble");
    u_audio_bands = glGetUniformLocation(program, "audio_bands");
    u_shockwaves = glGetUniformLocation(program, "u_shockwaves"); // НОВОЕ!

    // Новые фичи (MatCap и Инстансинг)
    u_use_matcap = glGetUniformLocation(program, "u_use_matcap");
    u_matcap_tex = glGetUniformLocation(program, "u_matcap_tex");
    u_use_instancing = glGetUniformLocation(program, "u_use_instancing");
    u_inner_scale = glGetUniformLocation(program, "u_inner_scale");
    u_outer_scale = glGetUniformLocation(program, "u_outer_scale");
}


bool IcoSphereEffect::reload_shader_program() {
    std::string config_dir = std::string(getenv("HOME")) + "/.config/interactive-wallpaper/";
    std::string base_path = config_dir + "effects/shaders/ico-sphere-effect/" + active_shader;
    
    std::cout << "[IcoSphere] Attempting to load shader theme: '" << active_shader << "'" << std::endl;
    
    std::string vert_src = shader_utils::load_shader_source(base_path + "_vert.glsl");
    std::string frag_src = shader_utils::load_shader_source(base_path + "_frag.glsl");
    
    if (vert_src.empty() || frag_src.empty()) {
        std::cerr << "[IcoSphere] Failed to load shader files for theme: " << active_shader << std::endl;
        return false;
    }
    
    GLuint new_program = shader_utils::create_shader_program(vert_src, frag_src);
    if (!new_program) {
        std::cerr << "[IcoSphere] Failed to compile new shader theme." << std::endl;
        return false;
    }
    
    // If successful: delete the old program and set the new one
    if (program != 0) {
        glDeleteProgram(program);
    }
    
    program = new_program;
    fetch_uniform_locations(); // Update Uniform addresses for the new program
    
    std::cout << "[IcoSphere] Successfully switched to shader theme: '" << active_shader << "'" << std::endl;
    return true;
}

void IcoSphereEffect::update_buffers() {
    if (vao == 0) return;

    glBindVertexArray(vao);
    
    // Загружаем упакованную структуру Vertex (Interleaved VBO)
    // 40 байт на вершину = максимальная скорость чтения из кэша GPU
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(), GL_STATIC_DRAW);
    
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

    // layout 0: position (vec3)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, position));
    glEnableVertexAttribArray(0);
    // layout 1: phase (float)
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, phase));
    glEnableVertexAttribArray(1);
    // layout 2: normal (vec3)
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));
    glEnableVertexAttribArray(2);
    // layout 3: barycentric (vec3) -> НОВОЕ!
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, barycentric));
    glEnableVertexAttribArray(3);
    
    glBindVertexArray(0);
}

void IcoSphereEffect::trigger_shockwave(const glm::vec3& hit_point) {
    // Записываем координаты удара на сфере и сбрасываем таймер волны (w = 0.0f)
    // Используется кольцевой буфер на 4 волны, чтобы они красиво пересекались
    shockwaves[current_shockwave_idx] = glm::vec4(glm::normalize(hit_point), 0.0f);
    current_shockwave_idx = (current_shockwave_idx + 1) % 4;
}

void IcoSphereEffect::update_shockwaves(float dt) {
    for (auto& wave : shockwaves) {
        // Если волна активна (время меньше 5 секунд), увеличиваем таймер
        if (wave.w < 5.0f) {
            wave.w += dt;
        }
    }
}

void IcoSphereEffect::load_matcap_from_file(const std::string& path) {
    if (path.empty() || path == matcap_filename) return;

    std::string full_path = std::string(getenv("HOME")) + "/.config/interactive-wallpaper/textures/" + path;
    
    int width, height, channels;
    // Переворачиваем текстуру по вертикали для стандарта OpenGL
    stbi_set_flip_vertically_on_load(true);
    unsigned char* data = stbi_load(full_path.c_str(), &width, &height, &channels, 0);

    if (!data) {
        std::cerr << "[MatCap] Failed to load texture: " << full_path << std::endl;
        use_matcap = false;
        return;
    }

    if (matcap_texture_id == 0) glGenTextures(1, &matcap_texture_id);
    
    glBindTexture(GL_TEXTURE_2D, matcap_texture_id);
    GLenum format = (channels == 4) ? GL_RGBA : GL_RGB;
    glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
    
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    glBindTexture(GL_TEXTURE_2D, 0);
    stbi_image_free(data);

    matcap_filename = path;
    use_matcap = true;
    std::cout << "[MatCap] Successfully loaded: " << path << std::endl;
}

bool IcoSphereEffect::init_bloom_resources(uint32_t width, uint32_t height) {
    // ВАЖНО ДЛЯ WAYLAND: Разрешение Bloom-буфера уменьшается в 4 раза (X/4, Y/4).
    // Это дает мягкое неоновое свечение и практически не тратит батарею ноутбука.
    uint32_t bloom_w = std::max(1u, width / 4);
    uint32_t bloom_h = std::max(1u, height / 4);

    if (bloom.initialized && bloom.width == bloom_w && bloom.height == bloom_h) return true;
    destroy_bloom_resources();

    bloom.width = bloom_w;
    bloom.height = bloom_h;

    // 1. Создаем основной FBO для сцены
    glGenFramebuffers(1, &bloom.fbo_scene);
    glBindFramebuffer(GL_FRAMEBUFFER, bloom.fbo_scene);

    glGenTextures(1, &bloom.tex_scene_color);
    glBindTexture(GL_TEXTURE_2D, bloom.tex_scene_color);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, bloom.tex_scene_color, 0);

    glGenRenderbuffers(1, &bloom.rbo_depth);
    glBindRenderbuffer(GL_RENDERBUFFER, bloom.rbo_depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, bloom.rbo_depth);

    // 2. Создаем Ping-Pong FBO для размытия Гаусса (уменьшенное разрешение)
    glGenFramebuffers(2, bloom.fbo_pingpong);
    glGenTextures(2, bloom.tex_pingpong);
    for (int i = 0; i < 2; i++) {
        glBindFramebuffer(GL_FRAMEBUFFER, bloom.fbo_pingpong[i]);
        glBindTexture(GL_TEXTURE_2D, bloom.tex_pingpong[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, bloom_w, bloom_h, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, bloom.tex_pingpong[i], 0);
    }

    // 3. Компилируем встроенные инлайн-шейдеры для Bloom (чтобы не плодить файлы)
    const char* vs_src = "#version 300 es\n out vec2 UV;\n void main() { float x = float((gl_VertexID & 1) << 2); float y = float((gl_VertexID & 2) << 1); UV = vec2(x * 0.5, y * 0.5); gl_Position = vec4(x - 1.0, y - 1.0, 0.0, 1.0); }";
    const char* fs_blur_src = "#version 300 es\n precision mediump float;\n in vec2 UV;\n uniform sampler2D image;\n uniform bool horizontal;\n uniform vec2 tex_offset;\n out vec4 FragColor;\n void main() { float weight[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216); vec3 result = texture(image, UV).rgb * weight[0]; if(horizontal) { for(int i=1; i<5; ++i) { result += texture(image, UV + vec2(tex_offset.x * float(i), 0.0)).rgb * weight[i]; result += texture(image, UV - vec2(tex_offset.x * float(i), 0.0)).rgb * weight[i]; } } else { for(int i=1; i<5; ++i) { result += texture(image, UV + vec2(0.0, tex_offset.y * float(i))).rgb * weight[i]; result += texture(image, UV - vec2(0.0, tex_offset.y * float(i))).rgb * weight[i]; } } FragColor = vec4(result, 1.0); }";
    const char* fs_final_src = "#version 300 es\n precision mediump float;\n in vec2 UV;\n uniform sampler2D scene;\n uniform sampler2D bloom;\n uniform float intensity;\n out vec4 FragColor;\n void main() { vec3 hdr = texture(scene, UV).rgb; vec3 glow = texture(bloom, UV).rgb; FragColor = vec4(hdr + glow * intensity, 1.0); }";

    if (!bloom_blur_program) bloom_blur_program = shader_utils::create_shader_program(vs_src, fs_blur_src);
    if (!bloom_final_program) bloom_final_program = shader_utils::create_shader_program(vs_src, fs_final_src);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    bloom.initialized = true;
    return true;
}

void IcoSphereEffect::destroy_bloom_resources() {
    if (!bloom.initialized) return;
    glDeleteFramebuffers(1, &bloom.fbo_scene);
    glDeleteTextures(1, &bloom.tex_scene_color);
    glDeleteRenderbuffers(1, &bloom.rbo_depth);
    glDeleteFramebuffers(2, bloom.fbo_pingpong);
    glDeleteTextures(2, bloom.tex_pingpong);
    bloom.initialized = false;
}

void IcoSphereEffect::render_bloom_postprocess(uint32_t width, uint32_t height) {
    glDisable(GL_DEPTH_TEST);
    glUseProgram(bloom_blur_program);
    glUniform2f(glGetUniformLocation(bloom_blur_program, "tex_offset"), 1.0f / bloom.width, 1.0f / bloom.height);

    // 10 итераций размытия Гаусса (5 горизонтальных + 5 вертикальных)
    bool horizontal = true, first_iteration = true;
    int amount = 10;
    glViewport(0, 0, bloom.width, bloom.height);

    for (int i = 0; i < amount; i++) {
        glBindFramebuffer(GL_FRAMEBUFFER, bloom.fbo_pingpong[horizontal]);
        glUniform1i(glGetUniformLocation(bloom_blur_program, "horizontal"), horizontal);
        glBindTexture(GL_TEXTURE_2D, first_iteration ? bloom.tex_scene_color : bloom.tex_pingpong[!horizontal]);
        glDrawArrays(GL_TRIANGLES, 0, 3); // Рисуем fullscreen треугольник без VBO
        horizontal = !horizontal;
        if (first_iteration) first_iteration = false;
    }

    // Финальная композиция (Сцена + Свечение Bloom) на экран Wayland
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width, height);
    glUseProgram(bloom_final_program);
    glUniform1f(glGetUniformLocation(bloom_final_program, "intensity"), bloom_intensity);
    
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, bloom.tex_scene_color);
    glUniform1i(glGetUniformLocation(bloom_final_program, "scene"), 0);
    
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, bloom.tex_pingpong[!horizontal]);
    glUniform1i(glGetUniformLocation(bloom_final_program, "bloom"), 1);

    glDrawArrays(GL_TRIANGLES, 0, 3);
    glActiveTexture(GL_TEXTURE0);
}

void IcoSphereEffect::update_rotation(float dt) {
    // Apply constant rotation if configured
    if (constant_rotation_speed > 0.0f) {
        glm::vec3 constant_axis = glm::vec3(0.0f, 1.0f, 0.0f);
        angular_velocity += constant_axis * constant_rotation_speed * dt;
    }

    // Apply inertia decay
    angular_velocity *= rotation_decay;
    float speed = glm::length(angular_velocity);

    // If speed is below the minimum threshold
    if (speed < min_rotation_speed) {
        // If the minimum speed is essentially zero, stop the rotation completely
        if (min_rotation_speed <= 1e-5f) {
            angular_velocity = glm::vec3(0.0f);
            return; // Exit early, no rotation to apply
        } else {
            // Otherwise, if there's any motion, boost it to the minimum speed
            if (speed > 1e-5f) { // Avoid normalizing a zero vector
                angular_velocity = glm::normalize(angular_velocity) * min_rotation_speed;
            }
        }
    }

    // Clamp to maximum speed, but only if max_rotation_speed is positive
    if (max_rotation_speed > 0.0f && speed > max_rotation_speed) {
        angular_velocity = glm::normalize(angular_velocity) * max_rotation_speed;
    }

    // Recalculate speed in case it was clamped and check if there's motion
    speed = glm::length(angular_velocity);
    if (speed > 1e-5f) {
        glm::vec3 axis = glm::normalize(angular_velocity);
        float angle = speed * dt;
        glm::quat rotation_delta = glm::angleAxis(angle, axis);
        orientation = glm::normalize(rotation_delta * orientation);
    }
}


void IcoSphereEffect::render(uint32_t width, uint32_t height) {
    if (needs_shader_reload) { reload_shader_program(); needs_shader_reload = false; }
    if (needs_regeneration) { generate_icosphere(subdivisions); update_buffers(); needs_regeneration = false; }

    // Обработка мыши из BlackBoard
    if (p_accum_x && p_accum_y) {
        float dx = *p_accum_x - last_mouse_x;
        float dy = *p_accum_y - last_mouse_y;
        last_mouse_x = *p_accum_x; last_mouse_y = *p_accum_y;
        angular_velocity += glm::vec3(dy, dx, 0.0f);
    }

    update_rotation(0.016f);
    update_shockwaves(0.016f);
    time += 0.016f;

    // --- 1. НАЧАЛО РЕНДЕРА (В FBO если включен Bloom, иначе на экран Wayland) ---
    if (bloom_intensity > 0.0f) {
        init_bloom_resources(width, height);
        glBindFramebuffer(GL_FRAMEBUFFER, bloom.fbo_scene);
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    glEnable(GL_DEPTH_TEST);
    glViewport(0, 0, width, height);
    glClearColor(background_color.r, background_color.g, background_color.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glUseProgram(program);
    
    // Матрицы камеры
    glm::mat4 model = glm::toMat4(orientation);
    glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 3.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 projection = glm::perspective(glm::radians(45.0f), (float)width / (float)height, 0.1f, 100.0f);
    
    glUniformMatrix4fv(u_model, 1, GL_FALSE, &model[0][0]);
    glUniformMatrix4fv(u_view, 1, GL_FALSE, &view[0][0]);
    glUniformMatrix4fv(u_projection, 1, GL_FALSE, &projection[0][0]);
    glUniform1f(u_time, time);
    
    // Передача визуальных параметров
    glUniform3fv(u_wireframe_color, 1, &wireframe_color[0]);
    glUniform3fv(u_object_color, 1, &object_color[0]);
    glUniform1i(u_is_wireframe_pass, wireframe_mode ? 1 : 0);
    glUniform1f(u_sphere_scale, sphere_scale);
    glUniform1f(u_oscill_amp, scaled_oscill_amp); glUniform1f(u_oscill_freq, oscill_freq);
    glUniform1f(u_wave_amp, scaled_wave_amp);     glUniform1f(u_wave_freq, wave_freq);
    glUniform1f(u_twist_amp, scaled_twist_amp);   glUniform1f(u_pulse_amp, scaled_pulse_amp);
    glUniform1f(u_noise_amp, scaled_noise_amp);

    // Передача аудио и ударных волн
    glUniform1f(u_audio_bass, p_audio_bass ? *p_audio_bass : 0.0f);
    glUniform1f(u_audio_mid, p_audio_mid ? *p_audio_mid : 0.0f);
    glUniform1f(u_audio_treble, p_audio_treble ? *p_audio_treble : 0.0f);
    if (p_audio_bands) glUniform1fv(u_audio_bands, 64, p_audio_bands);
    if (u_shockwaves != -1) glUniform4fv(u_shockwaves, 4, &shockwaves[0][0]);

    // Передача MatCap хрома
    glUniform1i(u_use_matcap, use_matcap ? 1 : 0);
    if (use_matcap && matcap_texture_id > 0) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, matcap_texture_id);
        glUniform1i(u_matcap_tex, 0);
    }

    // --- 2. ОТРИСОВКА СФЕРЫ (Инстансинг или Обычный режим) ---
    glBindVertexArray(vao);
    if (use_instancing) {
        // Отрисовка двух сфер за 1 Draw Call ("Ядро в клетке")
        glUniform1i(u_use_instancing, 1);
        glUniform1f(u_inner_scale, inner_scale);
        glUniform1f(u_outer_scale, outer_scale);
        glDrawElementsInstanced(GL_TRIANGLES, indices.size(), GL_UNSIGNED_INT, 0, 2);
    } else {
        glUniform1i(u_use_instancing, 0);
        glDrawElements(GL_TRIANGLES, indices.size(), GL_UNSIGNED_INT, 0);
    }
    glBindVertexArray(0);

    // --- 3. ЗАВЕРШЕНИЕ (Запуск Bloom пост-процессинга) ---
    if (bloom_intensity > 0.0f) {
        render_bloom_postprocess(width, height);
    }
    glDisable(GL_DEPTH_TEST);
}






void IcoSphereEffect::set_subdivisions(int value) { 
    int new_subdivisions = std::clamp(value, 0, 6);
    if (new_subdivisions != subdivisions) {
        subdivisions = new_subdivisions;
        needs_regeneration = true;
    }
}

void IcoSphereEffect::set_sphere_scale(float scale) { 
    if (std::abs(sphere_scale - scale) > 0.001f) {
        sphere_scale = scale;
        update_effect_scaling();
    }
}


// --- PLUGIN ADAPTER CLASS ---

class IcoSphereEffectPlugin : public IcoSphereEffect {
public:
    IcoSphereEffectPlugin() = default;
    ~IcoSphereEffectPlugin() override = default;

    const char* get_name() const override { return "Icosahedron Sphere"; }

    std::vector<EffectParameter> get_parameters() const override {
        return {
            {"shader_theme", "Shader variation name", active_shader},
            {"wireframe_mode", "Render as a wireframe", wireframe_mode},
            {"subdivisions", "Level of sphere detail (0-6)", subdivisions},
            {"sphere_scale", "Overall size of the sphere", sphere_scale},
            {"background_color", "Background clear color", background_color},
            {"wireframe_color", "Color of the wireframe lines", wireframe_color},
            {"object_color", "Color of the solid sphere surface", object_color},
            {"oscill_amp", "Oscillation Amplitude", oscill_amp},
            {"oscill_freq", "Oscillation Frequency", oscill_freq},
            {"wave_amp", "Wave Amplitude", wave_amp},
            {"wave_freq", "Wave Frequency", wave_freq},
            {"twist_amp", "Twist Amplitude", twist_amp},
            {"pulse_amp", "Pulse Amplitude", pulse_amp},
            {"noise_amp", "Noise Amplitude", noise_amp},
            {"rotation_decay", "Inertia decay (0.9-1.0)", rotation_decay},
            {"max_rotation_speed", "Maximum rotation speed", max_rotation_speed},
            {"min_rotation_speed", "Minimum rotation speed", min_rotation_speed},
            {"constant_rotation_speed", "Constant rotation speed", constant_rotation_speed},
            // --- НОВЫЕ ПАРАМЕТРЫ ДЛЯ LUA ---
            {"bloom_intensity", "Neon glow intensity (0.0 = off, 1.0+ = strong)", bloom_intensity},
            {"matcap_texture", "Filename of MatCap texture in ~/.config/.../textures/", matcap_filename},
            {"use_instancing", "Draw inner core and outer cage in 1 call", use_instancing},
            {"inner_scale", "Scale of the inner instanced core", inner_scale},
            {"outer_scale", "Scale of the outer instanced cage", outer_scale}
        };
    }

    void set_parameter(const std::string& name, const EffectParameterValue& value) override {
        try {
            if (name == "shader_theme") {
                std::string new_theme = std::get<std::string>(value);
                if (new_theme != active_shader) { active_shader = new_theme; needs_shader_reload = true; }
            }
            else if (name == "wireframe_mode")   { set_wireframe_mode(std::get<bool>(value)); }
            else if (name == "subdivisions")     { set_subdivisions(std::get<int>(value)); }
            else if (name == "sphere_scale")     { set_sphere_scale(std::get<float>(value)); }
            else if (name == "background_color") { set_background_color(std::get<glm::vec3>(value)); }
            else if (name == "wireframe_color")  { set_wireframe_color(std::get<glm::vec3>(value)); }
            else if (name == "object_color")     { set_object_color(std::get<glm::vec3>(value)); }
            else if (name == "oscill_amp")       { set_oscill_amp(std::get<float>(value)); }
            else if (name == "oscill_freq")      { set_oscill_freq(std::get<float>(value)); }
            else if (name == "wave_amp")         { set_wave_amp(std::get<float>(value)); }
            else if (name == "wave_freq")        { set_wave_freq(std::get<float>(value)); }
            else if (name == "twist_amp")        { set_twist_amp(std::get<float>(value)); }
            else if (name == "pulse_amp")        { set_pulse_amp(std::get<float>(value)); }
            else if (name == "noise_amp")        { set_noise_amp(std::get<float>(value)); }
            else if (name == "rotation_decay")   { set_rotation_decay(std::get<float>(value)); }
            else if (name == "max_rotation_speed") { set_max_rotation_speed(std::get<float>(value)); }
            else if (name == "min_rotation_speed") { set_min_rotation_speed(std::get<float>(value)); }
            else if (name == "constant_rotation_speed") { set_constant_rotation_speed(std::get<float>(value)); }
            // --- ОБРАБОТКА НОВЫХ ПАРАМЕТРОВ ---
            else if (name == "bloom_intensity")  { set_bloom_intensity(std::get<float>(value)); }
            else if (name == "matcap_texture")   { load_matcap_from_file(std::get<std::string>(value)); }
            else if (name == "use_instancing")   { set_use_instancing(std::get<bool>(value)); }
            else if (name == "inner_scale")      { set_inner_scale(std::get<float>(value)); }
            else if (name == "outer_scale")      { set_outer_scale(std::get<float>(value)); }
        } catch (const std::bad_variant_access& e) {
            std::cerr << "Warning: Type mismatch for parameter '" << name << "'. " << e.what() << std::endl;
        }
    }
};


// --- Exported C-functions ---
extern "C" {
    IWallpaperEffectABI* create_effect() {
        return new IcoSphereEffectPlugin(); 
    }
    void destroy_effect(IWallpaperEffectABI* effect) {
        // static_cast safely returns us to the class to call the destructor
        delete static_cast<WallpaperEffect*>(effect);
    }
}