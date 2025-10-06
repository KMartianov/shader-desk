// src/shader-utils.cpp
#include "shader-utils.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>

namespace shader_utils {

std::string load_shader_source(const std::string& relative_path) {
    // Сначала пробуем загрузить из системной конфигурационной директории
    std::string config_dir = std::string(getenv("HOME")) + "/.config/interactive-wallpaper/effects/shaders/";
    std::string filepath = config_dir + relative_path;
    
    std::ifstream file(filepath);
    if (file.is_open()) {
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::cout << "Loaded shader from: " << filepath << std::endl;
        return buffer.str();
    }
    
    // Fallback: пробуем загрузить из текущей рабочей директории (для разработки)
    file.open(relative_path);
    if (file.is_open()) {
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::cout << "Loaded shader from: " << relative_path << std::endl;
        return buffer.str();
    }
    
    std::cerr << "Failed to open shader file: " << filepath << " or " << relative_path << std::endl;
    return "";
}

GLuint compile_shader(GLenum type, const std::string& source) {
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

GLuint create_shader_program(const std::string& vertex_src, const std::string& fragment_src) {
    GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER, vertex_src);
    GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER, fragment_src);
    
    if (!vertex_shader || !fragment_shader) {
        if (vertex_shader) glDeleteShader(vertex_shader);
        if (fragment_shader) glDeleteShader(fragment_shader);
        return 0;
    }
    
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

} // namespace shader_utils