// src/shader-utils.hpp
#ifndef SHADER_UTILS_HPP
#define SHADER_UTILS_HPP

#include <GLES3/gl3.h>
#include <string>

namespace shader_utils {

// Загружает содержимое текстового файла (шейдера) в строку
std::string load_shader_source(const std::string& relative_path);

// Компилирует шейдер из исходного кода
GLuint compile_shader(GLenum type, const std::string& source);

// Создает шейдерную программу из вершинного и фрагментного шейдеров
GLuint create_shader_program(const std::string& vertex_src, const std::string& fragment_src);

} // namespace shader_utils

#endif // SHADER_UTILS_HPP