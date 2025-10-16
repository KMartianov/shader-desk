// src/ico-sphere-effect.cpp
// Этот файл объединяет логику эффекта и код плагина для компиляции в единый .so файл.
#define GLM_ENABLE_EXPERIMENTAL

// --- НЕОБХОДИМЫЕ ЗАГОЛОВКИ ---
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

// Вычисляет индекс средней точки ребра для подразделения икосферы
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


// --- РЕАЛИЗАЦИЯ КЛАССА IcoSphereEffect ---

IcoSphereEffect::IcoSphereEffect() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> dis(-1.0f, 1.0f);
    orientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    angular_velocity = glm::vec3(dis(gen), dis(gen), dis(gen)) * 0.1f;
    // Инициализация параметров по умолчанию
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
    mouse_sensitivity = 0.05f;
    touchpad_sensitivity = 10.0f;
    // Инициализация аудио-параметров
    audio_reactive = true;
    audio_smoothing = 0.85f;
    bass_multiplier = 1.0f;
    smoothed_bass = 0.0f;
    smoothed_mid = 0.0f;
    smoothed_treble = 0.0f;
    u_audio_bass = 0;
    u_audio_mid = 0;
    u_audio_treble = 0;


    update_effect_scaling();
}

void IcoSphereEffect::generate_icosphere(int subdivisions_level) {
    vertices.clear();
    indices.clear();
    phases.clear();
    normals.clear();
    line_indices.clear();
    const float t = (1.0f + std::sqrt(5.0f)) / 2.0f;

    vertices = {
        glm::normalize(glm::vec3(-1,  t,  0)), glm::normalize(glm::vec3( 1,  t,  0)),
        glm::normalize(glm::vec3(-1, -t,  0)), glm::normalize(glm::vec3( 1, -t,  0)),
        glm::normalize(glm::vec3( 0, -1,  t)), glm::normalize(glm::vec3( 0,  1,  t)),
        glm::normalize(glm::vec3( 0, -1, -t)), glm::normalize(glm::vec3( 0,  1, -t)),
        glm::normalize(glm::vec3( t,  0, -1)), glm::normalize(glm::vec3( t,  0,  1)),
        glm::normalize(glm::vec3(-t,  0, -1)), glm::normalize(glm::vec3(-t,  0,  1))
    };
    indices = {
        0, 11, 5,   0, 5, 1,    0, 1, 7,    0, 7, 10,   0, 10, 11,
        1, 5, 9,    5, 11, 4,   11, 10, 2,  10, 7, 6,   7, 1, 8,
        3, 9, 4,    3, 4, 2,    3, 2, 6,    3, 6, 8,    3, 8, 9,
        4, 9, 5,    2, 4, 11,   6, 2, 10,   8, 6, 7,    9, 8, 1
    };
    for (int i = 0; i < subdivisions_level; i++) {
        std::vector<unsigned int> new_indices;
        std::map<std::pair<unsigned int, unsigned int>, unsigned int> midpoint_cache;

        for (size_t j = 0; j < indices.size(); j += 3) {
            unsigned int i1 = indices[j], i2 = indices[j+1], i3 = indices[j+2];
            unsigned int im12 = get_midpoint_index(i1, i2, vertices, midpoint_cache);
            unsigned int im23 = get_midpoint_index(i2, i3, vertices, midpoint_cache);
            unsigned int im31 = get_midpoint_index(i3, i1, vertices, midpoint_cache);
            new_indices.insert(new_indices.end(), {i1, im12, im31});
            new_indices.insert(new_indices.end(), {im12, i2, im23});
            new_indices.insert(new_indices.end(), {im31, im23, i3});
            new_indices.insert(new_indices.end(), {im12, im23, im31});
        }
        indices = new_indices;
    }

    std::mt19937 gen(0);
    std::uniform_real_distribution<float> dis(0.0f, 2.0f * 3.1415926535f);
    phases.resize(vertices.size());
    for (size_t i = 0; i < vertices.size(); i++) {
        phases[i] = dis(gen);
    }

    normals.assign(vertices.size(), glm::vec3(0.0f));
    for (size_t i = 0; i < indices.size(); i += 3) {
        glm::vec3 v1 = vertices[indices[i]], v2 = vertices[indices[i+1]], v3 = vertices[indices[i+2]];
        glm::vec3 normal = glm::normalize(glm::cross(v2 - v1, v3 - v1));
        normals[indices[i]] += normal;
        normals[indices[i+1]] += normal;
        normals[indices[i+2]] += normal;
    }
    for (auto& normal : normals) {
        normal = glm::normalize(normal);
    }

    line_indices.clear();
    for (size_t i = 0; i < indices.size(); i += 3) {
        unsigned int i1 = indices[i], i2 = indices[i+1], i3 = indices[i+2];
        line_indices.push_back(i1); line_indices.push_back(i2);
        line_indices.push_back(i2); line_indices.push_back(i3);
        line_indices.push_back(i3); line_indices.push_back(i1);
    }
}

void IcoSphereEffect::update_effect_scaling() {
    scaled_oscill_amp = oscill_amp * sphere_scale;
    scaled_wave_amp = wave_amp * sphere_scale;
    scaled_twist_amp = twist_amp * sphere_scale;
    scaled_pulse_amp = pulse_amp * sphere_scale;
    scaled_noise_amp = noise_amp * sphere_scale;
}

bool IcoSphereEffect::initialize(uint32_t width, uint32_t height) {
    std::cout << "Initializing IcoSphere effect with size: " << width << "x" << height << std::endl;
    std::string config_dir = std::string(getenv("HOME")) + "/.config/interactive-wallpaper/";
    std::string vert_src = shader_utils::load_shader_source(config_dir + "effects/shaders/ico-sphere-effect/sphere_vert.glsl");
    std::string frag_src = shader_utils::load_shader_source(config_dir + "effects/shaders/ico-sphere-effect/sphere_frag.glsl");
    if (vert_src.empty() || frag_src.empty()) return false;
    
    program = shader_utils::create_shader_program(vert_src, frag_src);
    if (!program) return false;
    
    u_model = glGetUniformLocation(program, "model");
    u_view = glGetUniformLocation(program, "view");
    u_projection = glGetUniformLocation(program, "projection");
    u_time = glGetUniformLocation(program, "time");
    u_wireframe_color = glGetUniformLocation(program, "wireframe_color");
    u_is_wireframe_pass = glGetUniformLocation(program, "is_wireframe_pass");
    u_oscill_amp = glGetUniformLocation(program, "oscill_amp");
    u_oscill_freq = glGetUniformLocation(program, "oscill_freq");
    u_wave_amp = glGetUniformLocation(program, "wave_amp");
    u_wave_freq = glGetUniformLocation(program, "wave_freq");
    u_twist_amp = glGetUniformLocation(program, "twist_amp");
    u_pulse_amp = glGetUniformLocation(program, "pulse_amp");
    u_noise_amp = glGetUniformLocation(program, "noise_amp");
    u_sphere_scale = glGetUniformLocation(program, "sphere_scale");
    // Получаем ID новых uniform-переменных
    u_audio_bass = glGetUniformLocation(program, "audio_bass");
    u_audio_mid = glGetUniformLocation(program, "audio_mid");
    u_audio_treble = glGetUniformLocation(program, "audio_treble");
    generate_icosphere(subdivisions);
    
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);
    glGenBuffers(1, &line_ebo);

    update_buffers();
    
    std::cout << "IcoSphere effect initialized successfully." << std::endl;
    return true;
}

void IcoSphereEffect::update_buffers() {
    if (vao == 0) return;

    glBindVertexArray(vao);
    
    std::vector<float> vertex_data;
    vertex_data.reserve(vertices.size() * 7);
    // 3 pos + 1 phase + 3 normal
    for (size_t i = 0; i < vertices.size(); i++) {
        vertex_data.push_back(vertices[i].x);
        vertex_data.push_back(vertices[i].y);
        vertex_data.push_back(vertices[i].z);
        vertex_data.push_back(phases[i]);
        vertex_data.push_back(normals[i].x);
        vertex_data.push_back(normals[i].y);
        vertex_data.push_back(normals[i].z);
    }
    
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, vertex_data.size() * sizeof(float), vertex_data.data(), GL_STATIC_DRAW);
    
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, line_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, line_indices.size() * sizeof(unsigned int), line_indices.data(), GL_STATIC_DRAW);
    
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)(4 * sizeof(float)));
    glEnableVertexAttribArray(2);
    
    glBindVertexArray(0);
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
    if (needs_regeneration) {
        generate_icosphere(subdivisions);
        update_buffers();
        needs_regeneration = false;
        std::cout << "IcoSphere regenerated with " << subdivisions << " subdivisions." << std::endl;
    }

    update_rotation(0.016f);
    
    glEnable(GL_DEPTH_TEST);
    glViewport(0, 0, width, height);
    glClearColor(background_color.r, background_color.g, background_color.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glUseProgram(program);
    glm::mat4 model = glm::toMat4(orientation);
    glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 3.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 projection = glm::perspective(glm::radians(45.0f), (float)width / (float)height, 0.1f, 100.0f);
    
    glUniformMatrix4fv(u_model, 1, GL_FALSE, &model[0][0]);
    glUniformMatrix4fv(u_view, 1, GL_FALSE, &view[0][0]);
    glUniformMatrix4fv(u_projection, 1, GL_FALSE, &projection[0][0]);
    
    time += 0.016f;
    glUniform1f(u_time, time);
    
    // Передаём базовые параметры анимации из конфига
    glUniform1f(u_oscill_amp, scaled_oscill_amp);
    glUniform1f(u_oscill_freq, oscill_freq);
    glUniform1f(u_wave_amp, scaled_wave_amp);
    glUniform1f(u_wave_freq, wave_freq);
    glUniform1f(u_twist_amp, scaled_twist_amp);
    glUniform1f(u_pulse_amp, scaled_pulse_amp);
    glUniform1f(u_noise_amp, scaled_noise_amp);
    glUniform1f(u_sphere_scale, sphere_scale);
    // Передаём сглаженные аудиоданные напрямую в шейдер
    //std::cout << smoothed_bass << " " << smoothed_mid << " " << smoothed_treble << '\n';
    glUniform1f(u_audio_bass, smoothed_bass);
    glUniform1f(u_audio_mid, smoothed_mid);
    glUniform1f(u_audio_treble, smoothed_treble);

    glBindVertexArray(vao);
    if (wireframe_mode) {
        glUniform3f(u_wireframe_color, wireframe_color.r, wireframe_color.g, wireframe_color.b);
        glUniform1i(u_is_wireframe_pass, 1);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, line_ebo);
        glDrawElements(GL_LINES, line_indices.size(), GL_UNSIGNED_INT, 0);
    } else {
        glUniform1i(u_is_wireframe_pass, 0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        glDrawElements(GL_TRIANGLES, indices.size(), GL_UNSIGNED_INT, 0);
    }
    
    glBindVertexArray(0);
    glDisable(GL_DEPTH_TEST);
}

void IcoSphereEffect::cleanup() {
    if (program) glDeleteProgram(program);
    if (vao) glDeleteVertexArrays(1, &vao);
    if (vbo) glDeleteBuffers(1, &vbo);
    if (ebo) glDeleteBuffers(1, &ebo);
    if (line_ebo) glDeleteBuffers(1, &line_ebo);
    program = vao = vbo = ebo = line_ebo = 0;
    std::cout << "IcoSphere effect cleaned up." << std::endl;
}

void IcoSphereEffect::handle_pointer_motion(double dx, double dy, bool is_touchpad) {
    float sensitivity = is_touchpad ?
    touchpad_sensitivity : mouse_sensitivity;
    glm::vec3 impulse = glm::vec3(dy, dx, 0.0f) * sensitivity;
    angular_velocity += impulse;
}

void IcoSphereEffect::handle_audio_data(const AudioData& data) {
    if (!audio_reactive) {
        smoothed_bass = 0.0f;
        smoothed_mid = 0.0f;
        smoothed_treble = 0.0f;
        return;
    }

    // Сглаживаем полученные значения для более плавной анимации
    smoothed_bass = audio_smoothing * smoothed_bass + (1.0f - audio_smoothing) * static_cast<float>(data.bass);
    smoothed_mid = audio_smoothing * smoothed_mid + (1.0f - audio_smoothing) * static_cast<float>(data.mid);
    smoothed_treble = audio_smoothing * smoothed_treble + (1.0f - audio_smoothing) * static_cast<float>(data.treble);
}

void IcoSphereEffect::set_subdivisions(int value) { 
    int new_subdivisions = std::clamp(value, 0, 6);
    if (new_subdivisions != subdivisions) {
        subdivisions = new_subdivisions;
        needs_regeneration = true;
    }
}

// FIX 3: Removed the redundant IcoSphereEffect::compile_shader and 
// IcoSphereEffect::create_shader_program function definitions.
// The functionality is now provided by shader-utils.hpp.

// --- КЛАСС-АДАПТЕР ДЛЯ ПЛАГИНА ---

class IcoSphereEffectPlugin : public IcoSphereEffect {
public:
    IcoSphereEffectPlugin() = default;
    ~IcoSphereEffectPlugin() override = default;

    const char* get_name() const override {
        return "Icosahedron Sphere";
    }

    std::vector<EffectParameter> get_parameters() const override {
        return {
            {"wireframe_mode", "Render as a wireframe", wireframe_mode},
            {"subdivisions", "Level of sphere detail (0-6)", subdivisions},
            {"sphere_scale", "Overall size of the sphere", sphere_scale},
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
            {"background_color", "Background clear color", background_color},
            {"wireframe_color", "Color of the wireframe lines", wireframe_color},
            // Параметры для управления аудио-реакцией
            {"audio_reactive", "Enable audio reactivity", audio_reactive},
            {"audio_smoothing", "Smoothing factor for audio (0-1)", audio_smoothing}
        };
    }

    void set_parameter(const std::string& name, const EffectParameterValue& value) override {
        try {
            if (name == "wireframe_mode")   { set_wireframe_mode(std::get<bool>(value)); }
            else if (name == "subdivisions") { set_subdivisions(std::get<int>(value)); }
            else if (name == "sphere_scale") { set_sphere_scale(std::get<float>(value)); }
            else if (name == "oscill_amp")   { set_oscill_amp(std::get<float>(value)); }
            else if (name == "oscill_freq")  { set_oscill_freq(std::get<float>(value)); }
            else if (name == "wave_amp")     { set_wave_amp(std::get<float>(value)); }
            else if (name == "wave_freq")    { set_wave_freq(std::get<float>(value)); }
            else if (name == "twist_amp")    { set_twist_amp(std::get<float>(value)); }
            else if (name == "pulse_amp")    { set_pulse_amp(std::get<float>(value)); }
            else if (name == "noise_amp")    { set_noise_amp(std::get<float>(value)); }
            else if (name == "rotation_decay") { set_rotation_decay(std::get<float>(value)); }
            else if (name == "max_rotation_speed") { set_max_rotation_speed(std::get<float>(value)); }
            else if (name == "min_rotation_speed") { set_min_rotation_speed(std::get<float>(value)); }
            else if (name == "constant_rotation_speed") { set_constant_rotation_speed(std::get<float>(value)); }
            else if (name == "background_color") { set_background_color(std::get<glm::vec3>(value)); }
            else if (name == "wireframe_color")  { set_wireframe_color(std::get<glm::vec3>(value)); }
            else if (name == "audio_reactive")   { set_audio_reactive(std::get<bool>(value)); }
            else if (name == "audio_smoothing")  { set_audio_smoothing(std::get<float>(value)); }
            else {
                 std::cerr << "Warning: Unknown parameter '" << name << "'." << std::endl;
            }
        } catch (const std::bad_variant_access& e) {
            std::cerr << "Warning: Type mismatch for parameter '" << name << "'. " << e.what() << std::endl;
        }
    }
};

// --- ЭКСПОРТИРУЕМЫЕ СИ-ФУНКЦИИ ---

extern "C" {
    WallpaperEffect* create_effect() {
        return new IcoSphereEffectPlugin();
    }

    void destroy_effect(WallpaperEffect* effect) {
        delete effect;
    }
}