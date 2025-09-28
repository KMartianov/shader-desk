// src/ico-sphere-effect.cpp

#define GLM_ENABLE_EXPERIMENTAL

#include "ico-sphere-effect.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <map>
#include <vector>
#include <algorithm>

std::string load_shader_file(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "Failed to open shader file: " << filepath << std::endl;
        return "";
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}


IcoSphereEffect::IcoSphereEffect() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> dis(-10.0f, 10.0f); 

    orientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    angular_velocity = glm::vec3(dis(gen), dis(gen), dis(gen)); 

    

    update_effect_scaling();


    mouse_sensitivity = 0.05f;
    touchpad_sensitivity = 20.0f;
}

// Вспомогательная функция для получения индекса средней точки ребра
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

void IcoSphereEffect::generate_icosphere(int subdivisions) {
    vertices.clear();
    indices.clear();
    phases.clear();
    normals.clear();
    line_indices.clear();

    // Создание икосаэдра
    const float t = (1.0f + std::sqrt(5.0f)) / 2.0f;

    // 12 вершин икосаэдра
    vertices = {
        glm::normalize(glm::vec3(-1,  t,  0)), glm::normalize(glm::vec3( 1,  t,  0)),
        glm::normalize(glm::vec3(-1, -t,  0)), glm::normalize(glm::vec3( 1, -t,  0)),
        glm::normalize(glm::vec3( 0, -1,  t)), glm::normalize(glm::vec3( 0,  1,  t)),
        glm::normalize(glm::vec3( 0, -1, -t)), glm::normalize(glm::vec3( 0,  1, -t)),
        glm::normalize(glm::vec3( t,  0, -1)), glm::normalize(glm::vec3( t,  0,  1)),
        glm::normalize(glm::vec3(-t,  0, -1)), glm::normalize(glm::vec3(-t,  0,  1))
    };

    // 20 треугольников икосаэдра
    indices = {
        0, 11, 5,   0, 5, 1,    0, 1, 7,    0, 7, 10,   0, 10, 11,
        1, 5, 9,    5, 11, 4,   11, 10, 2,  10, 7, 6,   7, 1, 8,
        3, 9, 4,    3, 4, 2,    3, 2, 6,    3, 6, 8,    3, 8, 9,
        4, 9, 5,    2, 4, 11,   6, 2, 10,   8, 6, 7,    9, 8, 1
    };

    // Подразделение
    for (int i = 0; i < subdivisions; i++) {
        std::vector<unsigned int> new_indices;
        std::map<std::pair<unsigned int, unsigned int>, unsigned int> midpoint_cache;

        for (size_t j = 0; j < indices.size(); j += 3) {
            unsigned int i1 = indices[j];
            unsigned int i2 = indices[j+1];
            unsigned int i3 = indices[j+2];

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

    // Генерация случайных фаз для каждой вершины
    phases.resize(vertices.size());
    for (size_t i = 0; i < vertices.size(); i++) {
        phases[i] = static_cast<float>(rand()) / RAND_MAX * 2.0f * M_PI;
    }

    // Вычисление нормалей
    normals.resize(vertices.size(), glm::vec3(0.0f));
    for (size_t i = 0; i < indices.size(); i += 3) {
        glm::vec3 v1 = vertices[indices[i]];
        glm::vec3 v2 = vertices[indices[i+1]];
        glm::vec3 v3 = vertices[indices[i+2]];
        glm::vec3 normal = glm::normalize(glm::cross(v2 - v1, v3 - v1));
        normals[indices[i]] += normal;
        normals[indices[i+1]] += normal;
        normals[indices[i+2]] += normal;
    }
    for (auto& normal : normals) {
        normal = glm::normalize(normal);
    }

    // Генерация индексов для линий
    line_indices.clear();
    for (size_t i = 0; i < indices.size(); i += 3) {
        unsigned int i1 = indices[i];
        unsigned int i2 = indices[i+1];
        unsigned int i3 = indices[i+2];
        line_indices.push_back(i1); line_indices.push_back(i2);
        line_indices.push_back(i2); line_indices.push_back(i3);
        line_indices.push_back(i3); line_indices.push_back(i1);
    }
}

void IcoSphereEffect::update_effect_scaling() {
    /*
    scaled_oscill_amp = oscill_amp * sphere_scale;
    scaled_wave_amp = wave_amp * sphere_scale;
    scaled_twist_amp = twist_amp * sphere_scale;
    scaled_pulse_amp = pulse_amp * sphere_scale;
    scaled_noise_amp = noise_amp * sphere_scale;
    */
    scaled_oscill_amp = oscill_amp;
    scaled_wave_amp = wave_amp;
    scaled_twist_amp = twist_amp;
    scaled_pulse_amp = pulse_amp;
    scaled_noise_amp = noise_amp;

}

bool IcoSphereEffect::initialize(uint32_t width, uint32_t height) {
    std::cout << "Initializing IcoSphere effect with size: " << width << "x" << height << std::endl;
    std::cout << "Subdivisions level: " << subdivisions << std::endl;
    
    std::string vert_src = load_shader_file("shaders/sphere_vert.glsl");
    std::string frag_src = load_shader_file("shaders/sphere_frag.glsl");
    
    if (vert_src.empty() || frag_src.empty()) {
        std::cerr << "Failed to load sphere shaders" << std::endl;
        return false;
    }
    
    program = create_program(vert_src, frag_src);
    if (!program) {
        std::cerr << "Failed to create shader program" << std::endl;
        return false;
    }
    
    // Используем параметр из конфигурации вместо жесткого значения
    generate_icosphere(subdivisions);
    
    // Получение локаций uniform-переменных
    u_model = glGetUniformLocation(program, "model");
    u_view = glGetUniformLocation(program, "view");
    u_projection = glGetUniformLocation(program, "projection");
    u_time = glGetUniformLocation(program, "time");
    u_wireframe_color = glGetUniformLocation(program, "wireframe_color");
    u_background_color = glGetUniformLocation(program, "background_color");
    u_is_wireframe_pass = glGetUniformLocation(program, "is_wireframe_pass");

    u_oscill_amp = glGetUniformLocation(program, "oscill_amp");
    u_oscill_freq = glGetUniformLocation(program, "oscill_freq");
    u_wave_amp = glGetUniformLocation(program, "wave_amp");
    u_wave_freq = glGetUniformLocation(program, "wave_freq");
    u_twist_amp = glGetUniformLocation(program, "twist_amp");
    u_pulse_amp = glGetUniformLocation(program, "pulse_amp");
    u_noise_amp = glGetUniformLocation(program, "noise_amp");

    u_sphere_scale = glGetUniformLocation(program, "sphere_scale");

    
    // Локации для освещения
    GLuint u_lightColor = glGetUniformLocation(program, "lightColor");
    GLuint u_lightPos = glGetUniformLocation(program, "lightPos");
    GLuint u_viewPos = glGetUniformLocation(program, "viewPos");
    
    // Создание буферов
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);
    glGenBuffers(1, &line_ebo);

    glBindVertexArray(vao);
    
    // VBO: вершины + фазы + нормали (interleaved)
    std::vector<float> vertex_data;
    vertex_data.reserve(vertices.size() * 7); // 3 позиция + 1 фаза + 3 нормаль
    for (size_t i = 0; i < vertices.size(); i++) {
        // Позиция
        vertex_data.push_back(vertices[i].x);
        vertex_data.push_back(vertices[i].y);
        vertex_data.push_back(vertices[i].z);
        // Фаза
        vertex_data.push_back(phases[i]);
        // Нормаль
        vertex_data.push_back(normals[i].x);
        vertex_data.push_back(normals[i].y);
        vertex_data.push_back(normals[i].z);
    }
    
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, vertex_data.size() * sizeof(float), vertex_data.data(), GL_STATIC_DRAW);
    
    // EBO для треугольников
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

    // EBO для линий
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, line_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, line_indices.size() * sizeof(unsigned int), line_indices.data(), GL_STATIC_DRAW);
    
    // Атрибуты
    // Позиция (location = 0)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    // Фаза (location = 1)
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    // Нормаль (location = 2)
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)(4 * sizeof(float)));
    glEnableVertexAttribArray(2);
    
    glBindVertexArray(0);
    
    std::cout << "IcoSphere effect initialized with " << vertices.size() 
              << " vertices and " << indices.size() / 3 << " triangles" << std::endl;
    return true;
}

void IcoSphereEffect::update_buffers() {
    if (vao == 0) return;
    
    // Обновляем данные вершинного буфера
    std::vector<float> vertex_data;
    vertex_data.reserve(vertices.size() * 7);
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
    
    // Обновляем EBO для треугольников
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

    // Обновляем EBO для линий
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, line_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, line_indices.size() * sizeof(unsigned int), line_indices.data(), GL_STATIC_DRAW);
    
    std::cout << "Buffers updated with " << vertices.size() << " vertices" << std::endl;
}

void IcoSphereEffect::set_subdivisions(int value) { 
    int new_subdivisions = std::clamp(value, 0, 6);
    if (new_subdivisions != subdivisions) {
        subdivisions = new_subdivisions;
        needs_regeneration = true;
        std::cout << "Subdivisions set to: " << subdivisions << " (regeneration scheduled)" << std::endl;
    }
}

void IcoSphereEffect::update_rotation(float dt) {
    //glm::vec3 constant_rotation_axis = glm::vec3(0.0f, 1.0f, 0.0f);
    //angular_velocity += constant_rotation_axis * constant_rotation_speed * dt;

    float speed = glm::length(angular_velocity);

    // Применяем ограничения скорости
    if (speed > max_rotation_speed) {
        angular_velocity = glm::normalize(angular_velocity) * max_rotation_speed;
        speed = max_rotation_speed;
    }

    if (speed < min_rotation_speed && speed > 0.0001f) {
        angular_velocity = glm::normalize(angular_velocity) * min_rotation_speed;
        speed = min_rotation_speed;
    }

    if (speed < 0.0001f) {
        angular_velocity = glm::vec3(0.0f);
        return;
    }

    glm::vec3 axis = glm::normalize(angular_velocity);
    float angle = speed * dt;
    glm::quat rotation_delta = glm::angleAxis(angle, axis);

    orientation = rotation_delta * orientation;
    orientation = glm::normalize(orientation);

    angular_velocity *= rotation_decay;
  
}


void IcoSphereEffect::render(uint32_t width, uint32_t height) {
    // Шаг 1: Регенерация, если нужна (код из предыдущего ответа, оставляем его)
    if (needs_regeneration) {
        generate_icosphere(subdivisions);
        update_buffers();
        needs_regeneration = false;
        std::cout << "IcoSphere regenerated." << std::endl;
    }


    update_rotation(0.016f);
    glEnable(GL_DEPTH_TEST);
    glViewport(0, 0, width, height);
    glClearColor(background_color.r, background_color.g, background_color.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glUseProgram(program);


    glm::mat4 model = glm::toMat4(orientation);
    
    glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 3.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 projection = glm::perspective(glm::radians(45.0f), (float)width / (float)height, 0.1f, 100.0f);
    
    glUniformMatrix4fv(u_model, 1, GL_FALSE, &model[0][0]);
    glUniformMatrix4fv(u_view, 1, GL_FALSE, &view[0][0]);
    glUniformMatrix4fv(u_projection, 1, GL_FALSE, &projection[0][0]);
    
    time += 0.016f;
    glUniform1f(u_time, time);

    
    glUniform3f(glGetUniformLocation(program, "lightColor"), 1.0f, 1.0f, 1.0f);
    glUniform3f(glGetUniformLocation(program, "lightPos"), 2.0f, 2.0f, 2.0f);
    glUniform3f(glGetUniformLocation(program, "viewPos"), 0.0f, 0.0f, 3.0f);

    glUniform1f(u_oscill_amp, scaled_oscill_amp);
    glUniform1f(u_oscill_freq, oscill_freq);
    glUniform1f(u_wave_amp, scaled_wave_amp);
    glUniform1f(u_wave_freq, wave_freq);
    glUniform1f(u_twist_amp, scaled_twist_amp);
    glUniform1f(u_pulse_amp, scaled_pulse_amp);
    glUniform1f(u_noise_amp, scaled_noise_amp);

    glUniform1f(u_sphere_scale, sphere_scale);
    
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
    std::cout << "IcoSphere effect cleaned up" << std::endl;
}

void IcoSphereEffect::handle_pointer_move(double x, double y) {
}

void IcoSphereEffect::handle_pointer_click(double x, double y, uint32_t button) {
    
}

void IcoSphereEffect::handle_pointer_motion(double dx, double dy, bool is_touchpad) {
    // Здесь просто применяем вращение
    
    float rotation_strength = 0.1f;
    glm::vec3 mouse_rotation_impulse = glm::vec3(dy, dx, 0.0f) * rotation_strength;
    
    angular_velocity += mouse_rotation_impulse;
}

GLuint IcoSphereEffect::compile_shader(GLenum type, const std::string& source) {
    GLuint shader = glCreateShader(type);
    const char* src = source.c_str();
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(shader, 512, nullptr, infoLog);
        std::cerr << "Shader compilation failed:\n" << infoLog << std::endl;
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint IcoSphereEffect::create_program(const std::string& vertex_src, const std::string& fragment_src) {
    GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER, vertex_src);
    GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER, fragment_src);
    
    if (!vertex_shader || !fragment_shader) return 0;
    
    GLuint program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);
    
    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetProgramInfoLog(program, 512, nullptr, infoLog);
        std::cerr << "Program linking failed:\n" << infoLog << std::endl;
        glDeleteProgram(program);
        program = 0;
    }
    
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
    
    return program;
}