#ifndef HILBERT_CUBE_EFFECT_HPP
#define HILBERT_CUBE_EFFECT_HPP

#include "wallpaper-effect.hpp"
#include <GLES3/gl3.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <string>

class HilbertCubeEffect : public WallpaperEffect {
public:
    HilbertCubeEffect();
    ~HilbertCubeEffect() override;

    // --- Реализация интерфейса WallpaperEffect ---
    const char* get_name() const override;
    std::vector<EffectParameter> get_parameters() const override;
    void set_parameter(const std::string& name, const EffectParameterValue& value) override;

    bool initialize(uint32_t width, uint32_t height) override;
    void render(uint32_t width, uint32_t height) override;
    void cleanup() override;
    void handle_pointer_motion(double dx, double dy, bool is_touchpad) override;

private:
    // --- OpenGL объекты ---
    GLuint program = 0;
    GLuint curve_vao = 0, curve_vbo = 0;
    GLuint cube_vao = 0, cube_vbo = 0, cube_ebo = 0;
    GLsizei curve_vertex_count = 0;

    // --- Параметры эффекта ---
    int hilbert_order = 4;
    bool draw_cube_outline = true;
    glm::vec3 curve_color = {1.0f, 0.5f, 0.0f};
    glm::vec3 cube_color = {0.8f, 0.8f, 0.8f};
    bool needs_regeneration = true;

    // --- Логика вращения (аналогично IcoSphere) ---
    glm::quat orientation;
    glm::vec3 angular_velocity;
    float rotation_decay = 0.95f;
    float mouse_sensitivity = 0.05f;
    float touchpad_sensitivity = 10.0f;
    void update_rotation(float dt);

    // --- Uniform-переменные ---
    GLuint u_model = 0, u_view = 0, u_projection = 0;
    GLuint u_line_color = 0;

    // --- Генерация геометрии ---
    void generate_hilbert_curve();
    void generate_cube_outline();
    
    // Рекурсивная функция для генерации кривой
void hilbert3D(const glm::vec3& start, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, int order, std::vector<glm::vec3>& vertices);
};

// --- Экспортируемые C-функции для системы плагинов ---
extern "C" {
    WallpaperEffect* create_effect();
    void destroy_effect(WallpaperEffect* effect);
}

#endif // HILBERT_CUBE_EFFECT_HPP