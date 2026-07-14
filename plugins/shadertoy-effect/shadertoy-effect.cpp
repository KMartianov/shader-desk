// Src/plugins/shadertoy-effect/shadertoy-effect.cpp

#include "shadertoy-effect.hpp"
#include <shader-desk/shader-utils.hpp>
#include <iostream>
#include <regex>
#include <sstream>

// Lightweight image loading library. 
// Must be present in the plugin directory (wget https://raw.githubusercontent.com/nothings/stb/master/stb_image.h)
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h" 

// ==============================================================================
// 1. METADATA PARSING (Parameters and Channels)
// ==============================================================================

EffectParameterValue ShaderToyEffect::parse_default_value(const std::string& type_str, const std::string& val_str) {
    if (type_str == "float") return std::stof(val_str);
    if (type_str == "int") return std::stoi(val_str);
    if (type_str == "bool") return (val_str == "true" || val_str == "1");
    if (type_str == "vec3") {
        glm::vec3 v(0.0f);
        sscanf(val_str.c_str(), "%f,%f,%f", &v.x, &v.y, &v.z);
        return v;
    }
    return 0.0f;
}

void ShaderToyEffect::extract_metadata_from_source(const std::string& source) {
    dynamic_params.clear();
    for (int i = 0; i < 4; ++i) channels[i].type = ChannelType::NONE;

    // 1. Parse custom uniforms exported to Lua via @param annotations
    // Pattern: // @param name | type | default | description
    std::regex param_regex(R"(//\s*@param\s+([a-zA-Z0-9_]+)\s*\|\s*(float|int|bool|vec3)\s*\|\s*([^|]+)\s*\|\s*(.*))");
    
    for (std::sregex_iterator i = std::sregex_iterator(source.begin(), source.end(), param_regex); i != std::sregex_iterator(); ++i) {
        std::smatch match = *i;
        DynamicParam param;
        param.name = match[1].str();
        std::string type_str = match[2].str();
        std::string default_str = match[3].str();
        param.description = match[4].str();
        
        // Trim whitespace
        default_str.erase(0, default_str.find_first_not_of(" \t"));
        default_str.erase(default_str.find_last_not_of(" \t") + 1);

        try {
            param.value = parse_default_value(type_str, default_str);
            dynamic_params.push_back(param);
        } catch (...) {
            std::cerr << "[ShaderToy] Failed to parse default value for '" << param.name << "'\n";
        }
    }

    // 2. Parse ShaderToy channel declarations
    // Pattern: // @channelX | texture | filename.ext
    std::regex channel_regex(R"(//\s*@channel([0-3])\s*\|\s*(texture)\s*\|\s*(.*))");
    
    for (std::sregex_iterator i = std::sregex_iterator(source.begin(), source.end(), channel_regex); i != std::sregex_iterator(); ++i) {
        std::smatch match = *i;
        int idx = std::stoi(match[1].str());
        std::string type = match[2].str();
        std::string file = match[3].str();
        
        file.erase(0, file.find_first_not_of(" \t"));
        file.erase(file.find_last_not_of(" \t") + 1);

        if (type == "texture") {
            channels[idx].type = ChannelType::TEXTURE;
            channels[idx].source_file = file;
            
            // Flag for deferred loading. We don't load the file here because 
            // parsing happens before the EGL context is guaranteed to be current.
            channels[idx].pending_load = true; 
        }
    }
}

// ==============================================================================
// 2. SHADER GENERATION AND COMPILATION
// ==============================================================================

bool ShaderToyEffect::parse_and_compile(ICoreContext* core) {
    std::string raw_frag = shader_utils::load_shader_source(core, get_name(), target_shader_file);
    if (raw_frag.empty()) return false;

    extract_metadata_from_source(raw_frag);

    // Build the ShaderToy wrapper
    std::stringstream injected_frag;
    injected_frag << "#version 300 es\n";
    injected_frag << "precision highp float;\n";
    injected_frag << "out vec4 FragColor;\n";
    
    // Inject standard ShaderToy Uniforms
    injected_frag << "uniform vec3 iResolution;\n";
    injected_frag << "uniform float iTime;\n";
    injected_frag << "uniform float iTimeDelta;\n";
    injected_frag << "uniform int iFrame;\n";
    injected_frag << "uniform vec4 iMouse;\n";

    // Inject dynamic user Uniforms
    for (const auto& p : dynamic_params) {
        if (std::holds_alternative<float>(p.value)) injected_frag << "uniform float " << p.name << ";\n";
        else if (std::holds_alternative<int>(p.value)) injected_frag << "uniform int " << p.name << ";\n";
        else if (std::holds_alternative<bool>(p.value)) injected_frag << "uniform bool " << p.name << ";\n";
        else if (std::holds_alternative<glm::vec3>(p.value)) injected_frag << "uniform vec3 " << p.name << ";\n";
    }

    // Inject ShaderToy Channels (Textures)
    for (int i = 0; i < 4; ++i) {
        if (channels[i].type == ChannelType::TEXTURE) {
            injected_frag << "uniform sampler2D iChannel" << i << ";\n";
        }
    }

    // Append the user's raw GLSL code.
    // The #line 1 directive ensures that if compilation fails, the OpenGL driver's
    // error log will point to the correct line number in the user's original file.
    injected_frag << "\n#line 1\n" << raw_frag << "\n";

    // Entry point bridging the standard Wayland output to ShaderToy's signature
    injected_frag << R"(
    void main() {
        mainImage(FragColor, gl_FragCoord.xy);
    }
    )";

    // Zero-VBO Fullscreen Triangle vertex shader.
    // Generates a triangle covering the entire screen directly on the GPU, 
    // eliminating the need to transfer vertex data from the CPU.
    std::string vert_src = R"(
        #version 300 es
        void main() {
            float x = -1.0 + float((gl_VertexID & 1) << 2);
            float y = -1.0 + float((gl_VertexID & 2) << 1);
            gl_Position = vec4(x, y, 0.0, 1.0);
        }
    )";

    program = shader_utils::create_shader_program(vert_src, injected_frag.str());
    if (!program) return false;

    // Cache standard uniform locations to avoid expensive lookups during render loop
    u_iResolution = glGetUniformLocation(program, "iResolution");
    u_iTime = glGetUniformLocation(program, "iTime");
    u_iTimeDelta = glGetUniformLocation(program, "iTimeDelta");
    u_iFrame = glGetUniformLocation(program, "iFrame");
    u_iMouse = glGetUniformLocation(program, "iMouse");

    // Cache dynamic uniform locations
    for (auto& p : dynamic_params) {
        p.uniform_location = glGetUniformLocation(program, p.name.c_str());
    }

    // Cache texture sampler locations
    for (int i = 0; i < 4; ++i) {
        if (channels[i].type != ChannelType::NONE) {
            channels[i].uniform_location = glGetUniformLocation(program, ("iChannel" + std::to_string(i)).c_str());
        }
    }

    return true;
}

// ==============================================================================
// 3. DEFERRED TEXTURE LOADING (EGL SAFE CONTEXT)
// ==============================================================================

void ShaderToyEffect::process_pending_textures() {
    if (!m_core) return;
    
    // Resolve the absolute path to the plugin's isolated bundle directory
    std::string bundle_dir = m_core->get_bundle_path(get_name());
    
    for (int i = 0; i < 4; ++i) {
        if (channels[i].type == ChannelType::TEXTURE && channels[i].pending_load) {
            channels[i].pending_load = false;
            
            // Purge the old texture from VRAM to prevent memory leaks
            if (channels[i].texture_id) {
                glDeleteTextures(1, &channels[i].texture_id);
                channels[i].texture_id = 0;
            }

            if (channels[i].source_file.empty()) continue;

            std::string full_path = bundle_dir + "/shaders/" + channels[i].source_file;
            
            // Note: stbi_load is a blocking I/O operation. 
            // When triggered via Lua hot-reload, it will cause a 1-frame stutter.
            // This is an acceptable tradeoff for dynamic runtime texture swapping.
            int width, height, nrChannels;
            stbi_set_flip_vertically_on_load(true);
            unsigned char* data = stbi_load(full_path.c_str(), &width, &height, &nrChannels, 0);
            
            if (data) {
                glGenTextures(1, &channels[i].texture_id);
                glBindTexture(GL_TEXTURE_2D, channels[i].texture_id);
                
                // Standard texture parameters for seamless tiling and mipmapping
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);	
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                
                GLenum format = (nrChannels == 4) ? GL_RGBA : GL_RGB;
                glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
                glGenerateMipmap(GL_TEXTURE_2D);
                
                stbi_image_free(data);
                std::cout << "[ShaderToy] Successfully loaded texture: " << channels[i].source_file << "\n";
            } else {
                std::cerr << "[ShaderToy] Failed to load texture from disk: " << full_path << "\n";
            }
        }
    }
}

// ==============================================================================
// 4. MAIN RENDER LOOP AND LIFECYCLE
// ==============================================================================

bool ShaderToyEffect::initialize(ICoreContext* core, uint32_t width, uint32_t height) {
    if (program) return true;
    m_core = core; 
    
    // Bind to the Zero-Latency IPC BlackBoard for mouse tracking
    p_mouse_x = core->get_blackboard()->bind_float("mouse.accum_x");
    p_mouse_y = core->get_blackboard()->bind_float("mouse.accum_y");

    if (!parse_and_compile(core)) return false;
    
    // OpenGL ES 3.0 strictly requires a bound VAO to draw, even if it's empty
    glGenVertexArrays(1, &vao);
    return true;
}

void ShaderToyEffect::render(uint32_t width, uint32_t height, float dt) {
    // 1. Process IO and Texture Generation. 
    // Called here because the Microkernel guarantees the EGL context is active.

    if (pending_recompile) {
        pending_recompile = false;
        GLuint old_program = program; // Сохраняем старый шейдер на случай ошибки синтаксиса
        
        if (parse_and_compile(m_core)) {
            if (old_program) glDeleteProgram(old_program);
            std::cout << "[ShaderToy] Successfully hot-swapped shader to: " << target_shader_file << "\n";
        } else {
            program = old_program; // Откат к рабочему шейдеру
            std::cerr << "[ShaderToy] Failed to compile new shader. Keeping previous.\n";
        }
    }


    process_pending_textures();

    glUseProgram(program);
    glDisable(GL_DEPTH_TEST);

    time_accum += dt;
    frame_count++;

    // 2. Dispatch Standard ShaderToy Uniforms
    if (u_iResolution != -1) glUniform3f(u_iResolution, static_cast<float>(width), static_cast<float>(height), 1.0f);
    if (u_iTime != -1) glUniform1f(u_iTime, time_accum);
    if (u_iTimeDelta != -1) glUniform1f(u_iTimeDelta, dt);
    if (u_iFrame != -1) glUniform1i(u_iFrame, frame_count);
    
    if (u_iMouse != -1) {
        float mx = p_mouse_x ? *p_mouse_x : 0.0f;
        float my = p_mouse_y ? *p_mouse_y : 0.0f;
        glUniform4f(u_iMouse, mx, my, 0.0f, 0.0f);
    }

    // 3. Dispatch Dynamic User Uniforms
    for (const auto& p : dynamic_params) {
        if (p.uniform_location == -1) continue;

        if (std::holds_alternative<float>(p.value)) 
            glUniform1f(p.uniform_location, std::get<float>(p.value));
        else if (std::holds_alternative<int>(p.value)) 
            glUniform1i(p.uniform_location, std::get<int>(p.value));
        else if (std::holds_alternative<bool>(p.value)) 
            glUniform1i(p.uniform_location, std::get<bool>(p.value) ? 1 : 0);
        else if (std::holds_alternative<glm::vec3>(p.value)) {
            glm::vec3 v = std::get<glm::vec3>(p.value);
            glUniform3f(p.uniform_location, v.x, v.y, v.z);
        }
    }

    // 4. Bind Textures to corresponding Texture Units
    for (int i = 0; i < 4; ++i) {
        if (channels[i].type == ChannelType::TEXTURE && channels[i].texture_id) {
            glActiveTexture(GL_TEXTURE0 + i);
            glBindTexture(GL_TEXTURE_2D, channels[i].texture_id);
            glUniform1i(channels[i].uniform_location, i); // Inform the sampler about the unit
        }
    }

    // 5. Submit Draw Call
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    
    // 6. State Isolation: Reset the pipeline so subsequent layers aren't affected
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE0); 
}

void ShaderToyEffect::cleanup() {
    if (program) glDeleteProgram(program);
    if (vao) glDeleteVertexArrays(1, &vao);
    
    for (int i = 0; i < 4; ++i) {
        if (channels[i].texture_id) {
            glDeleteTextures(1, &channels[i].texture_id);
            channels[i].texture_id = 0;
        }
    }
    program = 0; 
    vao = 0;
}

// ==============================================================================
// 5. LUA API INTERFACE
// ==============================================================================

std::vector<EffectParameter> ShaderToyEffect::get_parameters() const {
    std::vector<EffectParameter> exports;
    
    // Allows hot-swapping the active shader file from Lua
    exports.push_back({"shader_file", "Target .glsl file inside the shaders/ directory", target_shader_file});
    
    // Export dynamically parsed uniforms
    for (const auto& p : dynamic_params) {
        exports.push_back({p.name, p.description, p.value});
    }

    // Export texture assignments
    for (int i = 0; i < 4; ++i) {
        if (channels[i].type == ChannelType::TEXTURE) {
            std::string name = "channel" + std::to_string(i) + "_tex";
            std::string desc = "Texture image file for iChannel" + std::to_string(i);
            exports.push_back({name, desc, channels[i].source_file});
        }
    }
    return exports;
}

void ShaderToyEffect::set_parameter(const std::string& name, const EffectParameterValue& value) {
    if (name == "shader_file") {
        std::string new_file = std::get<std::string>(value);
        if (target_shader_file != new_file) {
            target_shader_file = new_file;
            pending_recompile = true; // <--- Запрашиваем перекомпиляцию в следующем кадре
        }
        return;
    }


    // Wait-Free Texture Hot-Swapping:
    // If Lua modifies a channel parameter, we only update the string and flag it.
    // The actual blocking glGenTextures/stbi_load will happen safely in the next render() frame.
    if (name.rfind("channel", 0) == 0 && name.find("_tex") != std::string::npos) {
        int idx = name[7] - '0';
        if (idx >= 0 && idx < 4 && channels[idx].type == ChannelType::TEXTURE) {
            std::string new_file = std::get<std::string>(value);
            if (channels[idx].source_file != new_file) {
                channels[idx].source_file = new_file;
                channels[idx].pending_load = true; 
            }
        }
        return;
    }

    // Update dynamic uniforms
    for (auto& p : dynamic_params) {
        if (p.name == name) {
            p.value = value;
            break;
        }
    }
}

// ==============================================================================
// C-ABI EXPORTS (Hourglass Pattern)
// ==============================================================================
extern "C" {
    uint32_t get_abi_version() { return SHADER_DESK_ABI_VERSION; }
    IWallpaperEffectABI* create_effect() { return new ShaderToyEffect(); }
    void destroy_effect(IWallpaperEffectABI* effect) { delete static_cast<WallpaperEffect*>(effect); }
}