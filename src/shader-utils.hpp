// src/shader-utils.hpp
#ifndef SHADER_UTILS_HPP
#define SHADER_UTILS_HPP

#include <GLES3/gl3.h>
#include <string>
#include "plugin-abi.hpp"

namespace shader_utils {

// Loads text file (shader) content into a string
std::string load_shader_source(ICoreContextABI* core, const std::string& plugin_name, const std::string& shader_filename);

// Compiles shader from source code
GLuint compile_shader(GLenum type, const std::string& source);

// Creates a shader program from vertex and fragment shaders
GLuint create_shader_program(const std::string& vertex_src, const std::string& fragment_src);

} // namespace shader_utils

#endif // SHADER_UTILS_HPP