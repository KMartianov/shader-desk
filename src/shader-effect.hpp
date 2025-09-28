#pragma once
#include "interactive-wallpaper.hpp"
#include <GLES3/gl3.h>
#include <string>
#include <fstream>
#include <sstream>
#include <cstdint> // for uint32_t

class ShaderEffect : public WallpaperEffect {
private:
    GLuint program = 0;
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;
    float time = 0.0f;
    
    std::string load_shader_file(const std::string& filepath);
    GLuint compile_shader(GLenum type, const std::string& source);
    GLuint create_program(const std::string& vertex_src, const std::string& fragment_src);

public:
    std::string vertex_shader_path;
    std::string fragment_shader_path;
    
    ShaderEffect(const std::string& vert_path, const std::string& frag_path);
    bool initialize(uint32_t width, uint32_t height) override;
    void render(uint32_t width, uint32_t height) override;
    void cleanup() override;

    // Input handling (прототипы для реализаций в shader-effect.cpp)
    void handle_pointer_move(double x, double y);
    void handle_pointer_click(double x, double y, uint32_t button);
    void handle_pointer_motion(double dx, double dy);
};
