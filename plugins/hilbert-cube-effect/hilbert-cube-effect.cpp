#define GLM_ENABLE_EXPERIMENTAL


#include "hilbert-cube-effect.hpp"
#include "shader-utils.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <iostream>
#include <cmath>
#include <algorithm>

HilbertCubeEffect::HilbertCubeEffect() {
    orientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    angular_velocity = glm::vec3(0.1f, 0.1f, 0.05f);
}

HilbertCubeEffect::~HilbertCubeEffect() {
    cleanup();
}

// --- Алгоритм генерации 3D-кривой Гильберта (ИСПРАВЛЕННАЯ ВЕРСИЯ) ---

void HilbertCubeEffect::hilbert3D(const glm::vec3& start, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c,
                                 int order, std::vector<glm::vec3>& vertices)
{
    // Условие выхода из рекурсии: генерируем базовый сегмент кривой.
    if (order <= 1) {
        // Добавляем 8 вершин в порядке, формирующем базовый элемент кривой.
        // Путь проходит по ребрам куба.
        vertices.push_back(start);
        vertices.push_back(start + a);
        vertices.push_back(start + a + b);
        vertices.push_back(start + b);
        vertices.push_back(start + b + c);
        vertices.push_back(start + a + b + c);
        vertices.push_back(start + a + c);
        vertices.push_back(start + c);
    } else {
        // Рекурсивный шаг: делим куб на 8 меньших под-кубов и вызываем hilbert3D для каждого,
        // соответствующим образом трансформируя (смещая, поворачивая и отражая) их системы координат.

        // Эта специфическая реализация использует нестандартный масштабный коэффициент 1/3,
        // который был подобран для получения нужного визуального результата.
        const glm::vec3 a_s = a / 3.0f;
        const glm::vec3 b_s = b / 3.0f;
        const glm::vec3 c_s = c / 3.0f;

        // Предварительно вычисляем начальные точки для 8 рекурсивных вызовов.
        const glm::vec3 p0 = start;
        const glm::vec3 p1 = start + a * (2.0f / 3.0f);
        const glm::vec3 p2 = start + (a + b) * (2.0f / 3.0f);
        const glm::vec3 p3 = start + b + a_s;
        const glm::vec3 p4 = start + b + a_s + c * (2.0f / 3.0f);
        const glm::vec3 p5 = start + b + c + a_s * 2.0f;
        const glm::vec3 p6 = start + c + b_s + a_s * 2.0f;
        const glm::vec3 p7 = start + c + a_s;

        // 8 рекурсивных вызовов с трансформированными осями
        hilbert3D(p0,  b_s,  c_s,  a_s, order - 1, vertices);
        hilbert3D(p1,  c_s,  a_s,  b_s, order - 1, vertices);
        hilbert3D(p2,  c_s,  a_s,  b_s, order - 1, vertices);
        hilbert3D(p3, -a_s, -b_s,  c_s, order - 1, vertices);
        hilbert3D(p4, -a_s, -b_s,  c_s, order - 1, vertices);
        hilbert3D(p5, -c_s,  a_s, -b_s, order - 1, vertices);
        hilbert3D(p6, -c_s,  a_s, -b_s, order - 1, vertices);
        hilbert3D(p7,  b_s, -c_s, -a_s, order - 1, vertices);
    }
}



void HilbertCubeEffect::generate_hilbert_curve() {
    std::vector<glm::vec3> vertices;
    float size = 1.0f;
    glm::vec3 start_pos(-size / 2.0f, -size / 2.0f, -size / 2.0f);
    glm::vec3 a(size, 0, 0), b(0, size, 0), c(0, 0, size);
    
    hilbert3D(start_pos, a, b, c, hilbert_order, vertices);

    curve_vertex_count = vertices.size();

    glGenVertexArrays(1, &curve_vao);
    glGenBuffers(1, &curve_vbo);

    glBindVertexArray(curve_vao);
    glBindBuffer(GL_ARRAY_BUFFER, curve_vbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(glm::vec3), vertices.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
}

void HilbertCubeEffect::generate_cube_outline() {
    float s = 0.6f;
    std::vector<glm::vec3> vertices = {
        {-s, -s, -s}, {s, -s, -s}, {s, s, -s}, {-s, s, -s},
        {-s, -s,  s}, {s, -s,  s}, {s, s,  s}, {-s, s,  s}
    };
    std::vector<unsigned int> indices = {
        0, 1, 1, 2, 2, 3, 3, 0, // bottom face
        4, 5, 5, 6, 6, 7, 7, 4, // top face
        0, 4, 1, 5, 2, 6, 3, 7  // side edges
    };

    glGenVertexArrays(1, &cube_vao);
    glGenBuffers(1, &cube_vbo);
    glGenBuffers(1, &cube_ebo);

    glBindVertexArray(cube_vao);
    glBindBuffer(GL_ARRAY_BUFFER, cube_vbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(glm::vec3), vertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, cube_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
}


bool HilbertCubeEffect::initialize(uint32_t width, uint32_t height) {
    // Add this check to prevent re-creating the program
    if (program) return true;

    std::string vert_src = shader_utils::load_shader_source("hilbert-cube-effect/cube_vert.glsl");
    std::string frag_src = shader_utils::load_shader_source("hilbert-cube-effect/cube_frag.glsl");
    if (vert_src.empty() || frag_src.empty()) return false;

    program = shader_utils::create_shader_program(vert_src, frag_src);
    if (!program) return false;

    u_model = glGetUniformLocation(program, "model");
    u_view = glGetUniformLocation(program, "view");
    u_projection = glGetUniformLocation(program, "projection");
    u_line_color = glGetUniformLocation(program, "line_color");

    // Remove `needs_regeneration = true;` from here.
    // The constructor already sets it, ensuring geometry is created on the first frame.
    return true;
}

void HilbertCubeEffect::update_rotation(float dt) {
    angular_velocity *= rotation_decay;
    float speed = glm::length(angular_velocity);
    if (speed > 1e-5f) {
        glm::vec3 axis = glm::normalize(angular_velocity);
        float angle = speed * dt;
        glm::quat rotation_delta = glm::angleAxis(angle, axis);
        orientation = glm::normalize(rotation_delta * orientation);
    }
}

void HilbertCubeEffect::render(uint32_t width, uint32_t height) {
    if (needs_regeneration) {
        // --- CORRECTED REGENERATION LOGIC ---
        // 1. Clean up *only* the old geometry buffers.
        if (curve_vao) glDeleteVertexArrays(1, &curve_vao);
        if (curve_vbo) glDeleteBuffers(1, &curve_vbo);
        if (cube_vao) glDeleteVertexArrays(1, &cube_vao);
        if (cube_vbo) glDeleteBuffers(1, &cube_vbo);
        if (cube_ebo) glDeleteBuffers(1, &cube_ebo);

        // 2. Regenerate the geometry.
        generate_hilbert_curve();
        generate_cube_outline();
        needs_regeneration = false;
    }

    // --- ADD THIS ---
    // Clear the color and depth buffers with a dark background color.
    glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    update_rotation(0.016f);
    
    glEnable(GL_DEPTH_TEST);
    glLineWidth(1.0f);
    
    glUseProgram(program);
    glm::mat4 model = glm::toMat4(orientation);
    glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 2.5f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 projection = glm::perspective(glm::radians(45.0f), (float)width / (float)height, 0.1f, 100.0f);

    glUniformMatrix4fv(u_model, 1, GL_FALSE, &model[0][0]);
    glUniformMatrix4fv(u_view, 1, GL_FALSE, &view[0][0]);
    glUniformMatrix4fv(u_projection, 1, GL_FALSE, &projection[0][0]);
    
    // 1. Рисуем кривую Гильберта
    glUniform3fv(u_line_color, 1, &curve_color[0]);
    glBindVertexArray(curve_vao);
    glDrawArrays(GL_LINE_STRIP, 0, curve_vertex_count);

    // 2. Рисуем контур куба (если включено)
    if (draw_cube_outline) {
        glUniform3fv(u_line_color, 1, &cube_color[0]);
        glBindVertexArray(cube_vao);
        glDrawElements(GL_LINES, 24, GL_UNSIGNED_INT, 0);
    }
    
    glBindVertexArray(0);
    glDisable(GL_DEPTH_TEST);
}

void HilbertCubeEffect::cleanup() {
    if (program) glDeleteProgram(program);
    if (curve_vao) glDeleteVertexArrays(1, &curve_vao);
    if (curve_vbo) glDeleteBuffers(1, &curve_vbo);
    if (cube_vao) glDeleteVertexArrays(1, &cube_vao);
    if (cube_vbo) glDeleteBuffers(1, &cube_vbo);
    if (cube_ebo) glDeleteBuffers(1, &cube_ebo);
    program = curve_vao = curve_vbo = cube_vao = cube_vbo = cube_ebo = 0;
}

void HilbertCubeEffect::handle_pointer_motion(double dx, double dy, bool is_touchpad) {
    float sensitivity = is_touchpad ? touchpad_sensitivity : mouse_sensitivity;
    glm::vec3 impulse = glm::vec3(dy, dx, 0.0f) * sensitivity;
    angular_velocity += impulse;
}

// --- Интерфейс плагина ---
const char* HilbertCubeEffect::get_name() const {
    return "Hilbert Cube";
}

std::vector<EffectParameter> HilbertCubeEffect::get_parameters() const {
    return {
        {"hilbert_order", "Detail of the Hilbert curve (1-5)", hilbert_order},
        {"draw_cube_outline", "Draw the cube's wireframe", draw_cube_outline},
        {"curve_color", "Color of the Hilbert curve", curve_color},
        {"cube_color", "Color of the cube wireframe", cube_color},
        {"rotation_decay", "Inertia decay (0.9-1.0)", rotation_decay}
    };
}

void HilbertCubeEffect::set_parameter(const std::string& name, const EffectParameterValue& value) {
    try {
        if (name == "hilbert_order") {
            int new_order = std::clamp(std::get<int>(value), 1, 5);
            if (new_order != hilbert_order) {
                hilbert_order = new_order;
                needs_regeneration = true;
            }
        } else if (name == "draw_cube_outline") {
            draw_cube_outline = std::get<bool>(value);
        } else if (name == "curve_color") {
            curve_color = std::get<glm::vec3>(value);
        } else if (name == "cube_color") {
            cube_color = std::get<glm::vec3>(value);
        } else if (name == "rotation_decay") {
            rotation_decay = std::get<float>(value);
        } else {
            std::cerr << "Warning: Unknown parameter '" << name << "'." << std::endl;
        }
    } catch (const std::bad_variant_access& e) {
        std::cerr << "Warning: Type mismatch for parameter '" << name << "'. " << e.what() << std::endl;
    }
}

// --- Экспортируемые C-функции ---
extern "C" {
    WallpaperEffect* create_effect() {
        return new HilbertCubeEffect();
    }
    void destroy_effect(WallpaperEffect* effect) {
        delete effect;
    }
}