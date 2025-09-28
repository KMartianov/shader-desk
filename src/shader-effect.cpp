#include "shader-effect.hpp"
#include <iostream>
#include <fstream>
#include <sstream>

ShaderEffect::ShaderEffect(const std::string& vert_path, const std::string& frag_path)
    : vertex_shader_path(vert_path), fragment_shader_path(frag_path) {}

std::string ShaderEffect::load_shader_file(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "Failed to open shader file: " << filepath << std::endl;
        return "";
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

bool ShaderEffect::initialize(uint32_t width, uint32_t height) {
    std::cout << "Initializing shader effect with size: " << width << "x" << height << std::endl;
    
    if (width == 0 || height == 0) {
        std::cerr << "Invalid dimensions for shader initialization" << std::endl;
        return false;
    }

    std::string vertex_src = load_shader_file(vertex_shader_path);
    std::string fragment_src = load_shader_file(fragment_shader_path);
    
    if (vertex_src.empty() || fragment_src.empty()) {
        std::cerr << "Failed to load shader sources" << std::endl;
        return false;
    }
    
    program = create_program(vertex_src, fragment_src);
    if (!program) {
        std::cerr << "Failed to create shader program" << std::endl;
        return false;
    }

    // Fullscreen quad vertices
    float vertices[] = {
        -1.0f, -1.0f,  // bottom left
         1.0f, -1.0f,  // bottom right
         1.0f,  1.0f,  // top right
        -1.0f,  1.0f   // top left
    };
    
    unsigned int indices[] = {
        0, 1, 2,  // first triangle
        2, 3, 0   // second triangle
    };
    
    // Generate buffers
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);
    
    // Bind VAO first
    glBindVertexArray(vao);
    
    // Bind and set vertex buffer
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    
    // Bind and set element buffer
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
    
    // Configure vertex attributes
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    
    // Unbind VAO
    glBindVertexArray(0);
    
    std::cout << "Shader effect initialized successfully" << std::endl;
    return true;
}

void ShaderEffect::render(uint32_t width, uint32_t height) {
    // Set viewport
    glViewport(0, 0, width, height);
    
    // Use shader program
    glUseProgram(program);
    
    // Update time uniform
    time += 0.016f; // ~60 FPS
    GLint time_loc = glGetUniformLocation(program, "time");
    if (time_loc != -1) {
        glUniform1f(time_loc, time);
    }
    
    // Update resolution uniform
    GLint resolution_loc = glGetUniformLocation(program, "resolution");
    if (resolution_loc != -1) {
        glUniform2f(resolution_loc, static_cast<float>(width), static_cast<float>(height));
    }
    
    // Draw fullscreen quad
    glBindVertexArray(vao);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}

void ShaderEffect::cleanup() {
    if (program) {
        glDeleteProgram(program);
        program = 0;
    }
    if (vao) {
        glDeleteVertexArrays(1, &vao);
        vao = 0;
    }
    if (vbo) {
        glDeleteBuffers(1, &vbo);
        vbo = 0;
    }
    if (ebo) {
        glDeleteBuffers(1, &ebo);
        ebo = 0;
    }
    
    std::cout << "Shader effect cleaned up" << std::endl;
}

// Input handling methods
void ShaderEffect::handle_pointer_move(double x, double y) {
    std::cout << "ShaderEffect: Pointer moved to (" << x << ", " << y << ")" << std::endl;
}

void ShaderEffect::handle_pointer_click(double x, double y, uint32_t button) {
    const char* button_name = "unknown";
    switch (button) {
        case 0x110: button_name = "left"; break;    // BTN_LEFT
        case 0x111: button_name = "right"; break;   // BTN_RIGHT
        case 0x112: button_name = "middle"; break;  // BTN_MIDDLE
    }
    std::cout << "ShaderEffect: Pointer clicked at (" << x << ", " << y << ") with " << button_name << " button" << std::endl;
}

void ShaderEffect::handle_pointer_motion(double dx, double dy) {
    std::cout << "ShaderEffect: Pointer motion vector (" << dx << ", " << dy << ")" << std::endl;
}

GLuint ShaderEffect::compile_shader(GLenum type, const std::string& source) {
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

GLuint ShaderEffect::create_program(const std::string& vertex_src, const std::string& fragment_src) {
    GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER, vertex_src);
    if (!vertex_shader) {
        std::cerr << "Failed to compile vertex shader" << std::endl;
        return 0;
    }
    
    GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER, fragment_src);
    if (!fragment_shader) {
        std::cerr << "Failed to compile fragment shader" << std::endl;
        glDeleteShader(vertex_shader);
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
    
    // Clean up shaders
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
    
    return program;
}