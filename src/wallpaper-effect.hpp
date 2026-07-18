// Src/wallpaper-effect.hpp
#pragma once

#include "plugin-abi.hpp"
#include "shader-utils.hpp" // Required for the Standard Pipeline compilation

#include <vector>
#include <string>
#include <variant>
#include <memory>
#include <functional>
#include <cstring>
#include <algorithm>
#include <iostream>

#include <GLES3/gl3.h>
#include <glm/glm.hpp>

// Alias for backward compatibility. Plugins can use ICoreContext as before.
using ICoreContext = ICoreContextABI;

// Data types configurable via Lua Control Plane
using EffectParameterValue = std::variant<bool, int, float, glm::vec2, glm::vec3, glm::vec4, std::string>;

// Smart pointer for the loader (Core) with a custom .so unloader mechanism
using WallpaperEffectPtr = std::unique_ptr<IWallpaperEffectABI, std::function<void(IWallpaperEffectABI*)>>;

// Structure describing a single configurable plugin parameter (C++ style)
struct EffectParameter {
    std::string name;
    std::string description;
    EffectParameterValue value;
};

// ==============================================================================
// VISUAL PLUGIN SDK (BASE CLASS)
// Provides a unified interface for Wayland Compositor integration, Zero-Latency
// parameter caching, FBO resolution scaling, and a Standard Render Pipeline.
// ==============================================================================
class WallpaperEffect : public IWallpaperEffectABI {
public:
    virtual ~WallpaperEffect() {
        // Safely destroy OpenGL resources upon plugin unloading to prevent memory leaks
        destroy_internal_fbo();
        destroy_standard_pipeline();
    }

    // --- Core Developer API (Must be implemented by the child plugin) ---
    virtual const char* get_name() const = 0;
    virtual std::vector<EffectParameter> get_parameters() const = 0;
    virtual void set_parameter(const std::string& name, const EffectParameterValue& value) = 0;
    virtual bool initialize(ICoreContext* core, uint32_t width, uint32_t height) = 0;
    virtual void cleanup() = 0;

    // Optional hooks for advanced plugins
    virtual void resize(uint32_t width, uint32_t height) override {}
    virtual void set_paused(bool paused) override {}

    // ==========================================================================
    // STANDARD RENDER PIPELINE (Template Method Pattern)
    // ==========================================================================
    // Provides a default zero-allocation render loop for standard 2D effects.
    // If a plugin requires complex 3D logic, compute shaders, or multiple passes, 
    // the developer simply overrides this method to gain Absolute Control.
    virtual void render(uint32_t width, uint32_t height, float dt) override {
        // 1. Hot-Reload Processing (Shadow-Commit Mechanism)
        // Ensures that if a user saves a broken .glsl file, the engine doesn't crash.
        if (m_pending_shader_reload) {
            execute_shader_reload();
        }

        // 2. FBO Resolution Hijacking (Encapsulated Scaling)
        // If the user set `layer_fbo_scale` in Lua, this intercepts the draw call
        // and redirects it to a smaller/larger internal texture dynamically.
        auto [target_w, target_h] = begin_scaled_pass(width, height);

        // 3. Standard Quad Pipeline Execution
        if (m_std_program != 0) {
            glUseProgram(m_std_program);
            
            // Dispatch built-in variables (Time & Resolution)
            m_std_time += dt;
            if (m_std_u_time != -1) glUniform1f(m_std_u_time, m_std_time);
            if (m_std_u_resolution != -1) glUniform2f(m_std_u_resolution, static_cast<float>(target_w), static_cast<float>(target_h));

            // 4. Inversion of Control Hook
            // Allows the child plugin to inject its specific uniforms (colors, intensities)
            bind_custom_uniforms();

            // Disable depth testing for 2D backgrounds to prevent Z-fighting artifacts
            glDisable(GL_DEPTH_TEST);

            // Execute the Zero-VBO Fullscreen Triangle Draw Call
            glBindVertexArray(m_std_vao);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            
            // Defensively isolate state to prevent leaking into subsequent plugins
            glBindVertexArray(0);
            glEnable(GL_DEPTH_TEST);
        }

        // 5. Composite the scaled internal texture back to the Wayland Microkernel
        end_scaled_pass();
    }

    // ==========================================================================
    // ABI PARAMETER CACHING & SERIALIZATION (Hourglass Pattern)
    // Protects the boundary between the Core and the dynamically loaded .so.
    // Translates STL containers (std::vector, std::string) into safe C-structs.
    // ==========================================================================
    uint32_t get_parameter_count_abi() const final {
        if (!cache_valid) {
            param_cache = get_parameters();
            cache_valid = true;
        }
        return static_cast<uint32_t>(param_cache.size());
    }

    void get_parameter_info_abi(uint32_t index, ParamInfoABI* out_info) const final {
        if (index >= param_cache.size()) return;
        const auto& p = param_cache[index];
        
        std::strncpy(out_info->name, p.name.c_str(), sizeof(out_info->name) - 1);
        out_info->name[sizeof(out_info->name) - 1] = '\0';
        
        std::strncpy(out_info->description, p.description.c_str(), sizeof(out_info->description) - 1);
        out_info->description[sizeof(out_info->description) - 1] = '\0';
        
        if (std::holds_alternative<bool>(p.value)) {
            out_info->default_value.type = ParamType::TYPE_BOOL;
            out_info->default_value.b_val = std::get<bool>(p.value);
        } else if (std::holds_alternative<int>(p.value)) {
            out_info->default_value.type = ParamType::TYPE_INT;
            out_info->default_value.i_val = std::get<int>(p.value);
        } else if (std::holds_alternative<float>(p.value)) {
            out_info->default_value.type = ParamType::TYPE_FLOAT;
            out_info->default_value.f_val = std::get<float>(p.value);
        } else if (std::holds_alternative<glm::vec2>(p.value)) {
            out_info->default_value.type = ParamType::TYPE_VEC2;
            auto v = std::get<glm::vec2>(p.value);
            out_info->default_value.vec2_val[0] = v.x;
            out_info->default_value.vec2_val[1] = v.y;
        } else if (std::holds_alternative<glm::vec3>(p.value)) {
            out_info->default_value.type = ParamType::TYPE_VEC3;
            auto v = std::get<glm::vec3>(p.value);
            out_info->default_value.vec3_val[0] = v.x;
            out_info->default_value.vec3_val[1] = v.y;
            out_info->default_value.vec3_val[2] = v.z;
        } else if (std::holds_alternative<glm::vec4>(p.value)) {
            out_info->default_value.type = ParamType::TYPE_VEC4;
            auto v = std::get<glm::vec4>(p.value);
            out_info->default_value.vec4_val[0] = v.x;
            out_info->default_value.vec4_val[1] = v.y;
            out_info->default_value.vec4_val[2] = v.z;
            out_info->default_value.vec4_val[3] = v.w;
        } else if (std::holds_alternative<std::string>(p.value)) {
            out_info->default_value.type = ParamType::TYPE_STRING;
            const std::string& str = std::get<std::string>(p.value);
            size_t max_len = sizeof(out_info->default_value.s_val) - 1;
            std::strncpy(out_info->default_value.s_val, str.c_str(), max_len);
            out_info->default_value.s_val[max_len] = '\0';
        }
    }

    void set_parameter_abi(const char* name, const ParamValueABI* value) final {
        EffectParameterValue cpp_val;
        switch (value->type) {
            case ParamType::TYPE_BOOL: cpp_val = value->b_val; break;
            case ParamType::TYPE_INT: cpp_val = value->i_val; break;
            case ParamType::TYPE_FLOAT: cpp_val = value->f_val; break;
            case ParamType::TYPE_VEC2: cpp_val = glm::vec2(value->vec2_val[0], value->vec2_val[1]); break;
            case ParamType::TYPE_VEC3: cpp_val = glm::vec3(value->vec3_val[0], value->vec3_val[1], value->vec3_val[2]); break;
            case ParamType::TYPE_VEC4: cpp_val = glm::vec4(value->vec4_val[0], value->vec4_val[1], value->vec4_val[2], value->vec4_val[3]); break;
            case ParamType::TYPE_STRING: cpp_val = std::string(value->s_val); break;
        }
        this->set_parameter(std::string(name), cpp_val);
    }

    bool get_parameter_abi(const char* name, ParamValueABI* out_value) const final {
        std::vector<EffectParameter> fresh_params = get_parameters(); 
        for (const auto& p : fresh_params) {
            if (p.name == name) {
                if (std::holds_alternative<bool>(p.value)) {
                    out_value->type = ParamType::TYPE_BOOL;
                    out_value->b_val = std::get<bool>(p.value);
                } else if (std::holds_alternative<int>(p.value)) {
                    out_value->type = ParamType::TYPE_INT;
                    out_value->i_val = std::get<int>(p.value);
                } else if (std::holds_alternative<float>(p.value)) {
                    out_value->type = ParamType::TYPE_FLOAT;
                    out_value->f_val = std::get<float>(p.value);
                } else if (std::holds_alternative<glm::vec2>(p.value)) {
                    out_value->type = ParamType::TYPE_VEC2;
                    auto v = std::get<glm::vec2>(p.value);
                    out_value->vec2_val[0] = v.x; out_value->vec2_val[1] = v.y;
                } else if (std::holds_alternative<glm::vec3>(p.value)) {
                    out_value->type = ParamType::TYPE_VEC3;
                    auto v = std::get<glm::vec3>(p.value);
                    out_value->vec3_val[0] = v.x; out_value->vec3_val[1] = v.y; out_value->vec3_val[2] = v.z;
                } else if (std::holds_alternative<glm::vec4>(p.value)) {
                    out_value->type = ParamType::TYPE_VEC4;
                    auto v = std::get<glm::vec4>(p.value);
                    out_value->vec4_val[0] = v.x; out_value->vec4_val[1] = v.y; out_value->vec4_val[2] = v.z; out_value->vec4_val[3] = v.w;
                } else if (std::holds_alternative<std::string>(p.value)) {
                    out_value->type = ParamType::TYPE_STRING;
                    size_t max_len = sizeof(out_value->s_val) - 1;
                    std::strncpy(out_value->s_val, std::get<std::string>(p.value).c_str(), max_len);
                    out_value->s_val[max_len] = '\0';
                }
                return true;
            }
        }
        return false;
    }

protected:
    ICoreContext* m_core_ptr = nullptr; // Retained internally for delayed operations (like hot-reloading)

    // ==========================================================================
    // STANDARD PIPELINE SDK (For Child Plugins)
    // ==========================================================================
    float layer_fbo_scale = 1.0f; 

    // Helper: Child plugins MUST call this inside their get_parameters() 
    // to expose the resolution scaling feature to the Lua Control Plane.
    void register_standard_params(std::vector<EffectParameter>& params) const {
        params.push_back({"layer_fbo_scale", "Internal render resolution scale (1.0 = native)", layer_fbo_scale});
    }

    // Helper: Child plugins MUST call this inside their set_parameter() 
    // to intercept the scale parameter before processing their own.
    bool apply_standard_param(const std::string& name, const EffectParameterValue& value) {
        if (name == "layer_fbo_scale") {
            try {
                // Clamp prevents users from accidentally allocating massive textures (e.g., 10000x scaling)
                layer_fbo_scale = std::clamp(std::get<float>(value), 0.05f, 4.0f);
                return true;
            } catch (...) {}
        }
        return false;
    }

    // Initializes the default pipeline.
    // Automatically injects required GLSL boilerplate if the user forgot it.
    // Falls back to a built-in Zero-VBO vertex shader if vert_filename is empty.
    bool init_standard_pipeline(ICoreContext* core, const std::string& vert_filename, const std::string& frag_filename) {
        m_core_ptr = core;
        m_active_vert_file = vert_filename;
        m_active_frag_file = frag_filename;

        std::string vert_src;
        if (!vert_filename.empty()) {
            vert_src = shader_utils::load_shader_source(core, get_name(), vert_filename);
        } else {
            // Built-in Zero-VBO Vertex Shader (Opt-in DX Feature)
            // Generates a fullscreen triangle dynamically without CPU VBO transfers.
            vert_src = R"(#version 300 es
                precision highp float;
                out vec2 v_uv;
                void main() {
                    float x = -1.0 + float((gl_VertexID & 1) << 2);
                    float y = -1.0 + float((gl_VertexID & 2) << 1);
                    // Standard Wayland Top-Left UV mapping
                    v_uv = vec2(x * 0.5 + 0.5, y * 0.5 + 0.5);
                    gl_Position = vec4(x, y, 0.0, 1.0);
                }
            )";
        }

        std::string frag_src = shader_utils::load_shader_source(core, get_name(), frag_filename);
        if (frag_src.empty()) return false;

        // DX Improvement: Auto-inject GLSL ES boilerplate to lower the barrier of entry
        // for ShaderToy artists who are not familiar with OpenGL ES strictness.
        if (frag_src.find("#version") == std::string::npos) {
            std::string boilerplate = 
                "#version 300 es\n"
                "precision highp float;\n"
                "in vec2 v_uv;\n"
                "out vec4 FragColor;\n";
            frag_src = boilerplate + frag_src;
        }

        GLuint new_prog = shader_utils::create_shader_program(vert_src, frag_src);
        if (!new_prog) return false;

        if (m_std_program) glDeleteProgram(m_std_program);
        m_std_program = new_prog;

        // Cache standard uniform locations
        m_std_u_time = glGetUniformLocation(m_std_program, "time");
        m_std_u_resolution = glGetUniformLocation(m_std_program, "resolution");

        // OpenGL ES 3.0 requires a bound VAO to draw, even if it has no VBOs attached
        if (!m_std_vao) glGenVertexArrays(1, &m_std_vao);

        return true;
    }

    // Queues a shader recompilation for the next frame.
    // Child plugins use this when Lua updates `shader_theme`.
    void request_shader_reload(const std::string& new_frag_filename, const std::string& new_vert_filename = "") {
        m_pending_frag_file = new_frag_filename;
        m_pending_vert_file = new_vert_filename.empty() ? m_active_vert_file : new_vert_filename;
        m_pending_shader_reload = true;
    }

    // Virtual Hook: Called immediately before glDrawArrays inside the template method.
    // Child plugins OVERRIDE this to dispatch their specific uniform variables.
    virtual void bind_custom_uniforms() {}

    // ==========================================================================
    // FBO RESOLUTION SCALING API (Encapsulated Pass)
    // ==========================================================================
    struct ScaledPassInfo {
        uint32_t width;
        uint32_t height;
    };

    // Hijacks the Core FBO and redirects all GL rendering to an isolated, scaled texture.
    // This allows demanding shaders to render at a fraction of the screen resolution.
    ScaledPassInfo begin_scaled_pass(uint32_t core_w, uint32_t core_h) {
        // Fast-Path: If scaling is native (1.0), bypass all FBO overhead
        if (layer_fbo_scale >= 0.99f && layer_fbo_scale <= 1.01f) {
            return {core_w, core_h}; 
        }

        uint32_t target_w = std::max<uint32_t>(1, static_cast<uint32_t>(core_w * layer_fbo_scale));
        uint32_t target_h = std::max<uint32_t>(1, static_cast<uint32_t>(core_h * layer_fbo_scale));

        // Reallocate internal textures only when the resolution physically changes
        if (target_w != m_internal_w || target_h != m_internal_h) {
            allocate_internal_fbo(target_w, target_h);
        }

        // Save Wayland Microkernel's OpenGL state to restore it seamlessly later
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &m_core_fbo);
        glGetIntegerv(GL_VIEWPORT, m_core_viewport);

        // Bind our isolated FBO sandbox
        glBindFramebuffer(GL_FRAMEBUFFER, m_internal_fbo);
        glViewport(0, 0, target_w, target_h);

        // Crucial: Clear with zero alpha so Wayland transparency and multi-layer composition works correctly
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        return {target_w, target_h};
    }

    // Projects the scaled internal texture back to the Wayland Microkernel's Ping-Pong FBO.
    void end_scaled_pass() {
        if (layer_fbo_scale >= 0.99f && layer_fbo_scale <= 1.01f) return;

        // Restore Wayland Microkernel's OpenGL state
        glBindFramebuffer(GL_FRAMEBUFFER, m_core_fbo);
        glViewport(m_core_viewport[0], m_core_viewport[1], m_core_viewport[2], m_core_viewport[3]);

        if (!m_blit_program) compile_blit_shader();

        glUseProgram(m_blit_program);
        
        // Emulate Alpha Blending. The internal texture might have transparent areas
        // that need to blend with the layers previously drawn by the Microkernel.
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_DEPTH_TEST);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_internal_tex);

        if (!m_blit_vao) glGenVertexArrays(1, &m_blit_vao);
        glBindVertexArray(m_blit_vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        
        // Isolate state to prevent corruption of the next plugin in the chain
        glBindVertexArray(0);
        glEnable(GL_DEPTH_TEST);
    }

private:
    mutable std::vector<EffectParameter> param_cache;
    mutable bool cache_valid = false;

    // --- Standard Pipeline State ---
    GLuint m_std_program = 0;
    GLuint m_std_vao = 0;
    GLint m_std_u_time = -1;
    GLint m_std_u_resolution = -1;
    float m_std_time = 0.0f;

    std::string m_active_frag_file = "";
    std::string m_active_vert_file = "";
    
    // --- Shadow-Commit State ---
    bool m_pending_shader_reload = false;
    std::string m_pending_frag_file = "";
    std::string m_pending_vert_file = "";

    // --- FBO Scaling State ---
    GLuint m_internal_fbo = 0;
    GLuint m_internal_tex = 0;
    GLuint m_internal_rbo = 0;
    uint32_t m_internal_w = 0;
    uint32_t m_internal_h = 0;
    
    GLint m_core_fbo = 0;
    GLint m_core_viewport[4] = {0, 0, 0, 0};

    GLuint m_blit_program = 0;
    GLuint m_blit_vao = 0;

    void destroy_standard_pipeline() {
        if (m_std_program) { glDeleteProgram(m_std_program); m_std_program = 0; }
        if (m_std_vao) { glDeleteVertexArrays(1, &m_std_vao); m_std_vao = 0; }
    }

    void execute_shader_reload() {
        m_pending_shader_reload = false;
        if (!m_core_ptr) return;

        GLuint old_program = m_std_program;
        
        // Shadow-Commit Logic: Attempt to compile the new shader.
        // This runs directly on the Wayland EGL context during the render loop.
        if (init_standard_pipeline(m_core_ptr, m_pending_vert_file, m_pending_frag_file)) {
            if (old_program) glDeleteProgram(old_program);
            std::cout << "\033[32m[" << get_name() << "] Shader hot-swapped to: " << m_pending_frag_file << "\033[0m\n";
        } else {
            // Shadow-Commit Rollback: If it failed (e.g., syntax error in .glsl), 
            // revert to the previously working shader program.
            m_std_program = old_program;
            
            // Re-bind uniform locations since we restored the old program
            if (m_std_program) {
                m_std_u_time = glGetUniformLocation(m_std_program, "time");
                m_std_u_resolution = glGetUniformLocation(m_std_program, "resolution");
            }
            std::cerr << "\033[31m[" << get_name() << "] Compilation failed. Reverting to previous shader.\033[0m\n";
        }
    }

    void allocate_internal_fbo(uint32_t w, uint32_t h) {
        destroy_internal_fbo();
        m_internal_w = w; m_internal_h = h;

        glGenFramebuffers(1, &m_internal_fbo);
        glGenTextures(1, &m_internal_tex);
        glGenRenderbuffers(1, &m_internal_rbo);

        glBindFramebuffer(GL_FRAMEBUFFER, m_internal_fbo);

        glBindTexture(GL_TEXTURE_2D, m_internal_tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        
        // Linear scaling is preferred for universal downsampling/upsampling
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_internal_tex, 0);

        // 3D Depth support is mandatory for plugins like Hilbert Cube or Raymarched Text
        glBindRenderbuffer(GL_RENDERBUFFER, m_internal_rbo);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_internal_rbo);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void destroy_internal_fbo() {
        if (m_internal_fbo) { glDeleteFramebuffers(1, &m_internal_fbo); m_internal_fbo = 0; }
        if (m_internal_tex) { glDeleteTextures(1, &m_internal_tex); m_internal_tex = 0; }
        if (m_internal_rbo) { glDeleteRenderbuffers(1, &m_internal_rbo); m_internal_rbo = 0; }
        if (m_blit_program) { glDeleteProgram(m_blit_program); m_blit_program = 0; }
        if (m_blit_vao) { glDeleteVertexArrays(1, &m_blit_vao); m_blit_vao = 0; }
    }

    void compile_blit_shader() {
        // Zero-VBO Fullscreen Quad Vertex Shader for the final blit
        const char* v_src = R"(#version 300 es
            out vec2 v_uv;
            void main() {
                float x = -1.0 + float((gl_VertexID & 1) << 2);
                float y = -1.0 + float((gl_VertexID & 2) << 1);
                v_uv = vec2(x * 0.5 + 0.5, y * 0.5 + 0.5);
                gl_Position = vec4(x, y, 0.0, 1.0);
            }
        )";
        
        // Basic Texture Sampler Fragment Shader
        const char* f_src = R"(#version 300 es
            precision highp float;
            in vec2 v_uv;
            out vec4 FragColor;
            uniform sampler2D u_tex;
            void main() { 
                FragColor = texture(u_tex, v_uv); 
            }
        )";

        GLuint vs = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vs, 1, &v_src, nullptr);
        glCompileShader(vs);

        GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fs, 1, &f_src, nullptr);
        glCompileShader(fs);

        m_blit_program = glCreateProgram();
        glAttachShader(m_blit_program, vs);
        glAttachShader(m_blit_program, fs);
        glLinkProgram(m_blit_program);

        glDeleteShader(vs);
        glDeleteShader(fs);
    }
};