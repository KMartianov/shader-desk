// src/shader-utils.cpp
#include "shader-utils.hpp"
#include "plugin-abi.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <filesystem> // <--- ВОТ ЭТОГО НЕ ХВАТАЛО!

namespace fs = std::filesystem;

namespace shader_utils {

std::string load_shader_source(ICoreContextABI* core, const std::string& plugin_name, const std::string& shader_filename) {
    std::string bundle_dir;
    if (core) {
        bundle_dir = core->get_bundle_path(plugin_name.c_str());
    }

    // 1. Поиск в папке бандла плагина: .../effects/<bundle>/shaders/<filename>
    if (!bundle_dir.empty()) {
        fs::path filepath = fs::path(bundle_dir) / "shaders" / shader_filename;
        std::ifstream file(filepath);
        if (file.is_open()) {
            std::stringstream buffer;
            buffer << file.rdbuf();
            std::cout << "[ShaderUtils] Loaded shader from bundle: " << filepath << std::endl;
            return buffer.str();
        } else {
            std::cerr << "[ShaderUtils] Warning: Shader not found in bundle path: " << filepath << std::endl;
        }
    }

    // 2. Fallback: поиск относительно текущей рабочей директории
    std::ifstream file(shader_filename);
    if (file.is_open()) {
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::cout << "[ShaderUtils] Loaded shader from local path: " << shader_filename << std::endl;
        return buffer.str();
    }
    
    std::cerr << "[ShaderUtils] CRITICAL: Failed to load shader '" << shader_filename 
              << "' for plugin '" << plugin_name << "'" << std::endl;
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