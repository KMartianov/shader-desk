// Src/ico-sphere-effect.cpp
// ==============================================================================
// ICOSPHERE EFFECT (ADVANCED)
// Features: Barycentric wireframes, Bloom post-processing, MatCap texturing,
// Hardware instancing, Audio reactivity, and Kinematic physics integration.
// ==============================================================================

#define GLM_ENABLE_EXPERIMENTAL

#include "ico-sphere-effect.hpp"
#include <shader-desk/shader-utils.hpp> 

#include <iostream>
#include <cmath>
#include <algorithm>
#include <random>
#include <map>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <cstddef> 

// Implementation of stb_image for MatCap texture loading
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// ==============================================================================
// GEOMETRY HELPERS
// ==============================================================================

// Calculates the midpoint index of an edge for icosphere subdivision.
// Uses a map to cache already calculated midpoints, avoiding duplicate vertices.
unsigned int get_midpoint_index(unsigned int i1, unsigned int i2, 
                                std::vector<glm::vec3>& vertices, 
                                std::map<std::pair<unsigned int, unsigned int>, unsigned int>& cache) {
    std::pair<unsigned int, unsigned int> edge_key(std::min(i1, i2), std::max(i1, i2));
    auto it = cache.find(edge_key);
    if (it != cache.end()) return it->second;

    glm::vec3 v1 = vertices[i1];
    glm::vec3 v2 = vertices[i2];
    glm::vec3 midpoint = glm::normalize((v1 + v2) * 0.5f); // Project onto the unit sphere
    
    unsigned int new_index = vertices.size();
    vertices.push_back(midpoint);
    cache[edge_key] = new_index;
    
    return new_index;
}

// ==============================================================================
// CONSTRUCTOR & DESTRUCTOR
// ==============================================================================

IcoSphereEffect::IcoSphereEffect() {
    // Initialize default visual state
    wireframe_mode = true;
    subdivisions = 3;
    sphere_scale = 1.0f;
    
    oscill_amp = 0.0f; oscill_freq = 1.0f;
    wave_amp = 0.0f;   wave_freq = 1.0f;
    twist_amp = 0.0f;  pulse_amp = 0.0f; noise_amp = 0.0f;
    
    update_effect_scaling();
}

IcoSphereEffect::~IcoSphereEffect() {
    on_cleanup();
}

// ==============================================================================
// CPU GEOMETRY GENERATION
// ==============================================================================

void IcoSphereEffect::generate_icosphere(int subdivisions_level) {
    vertices.clear();
    indices.clear();
    
    // 1. Generate base icosahedron (12 vertices, 20 triangles)
    const float t = (1.0f + std::sqrt(5.0f)) / 2.0f;
    std::vector<glm::vec3> base_pos = {
        glm::normalize(glm::vec3(-1,  t,  0)), glm::normalize(glm::vec3( 1,  t,  0)),
        glm::normalize(glm::vec3(-1, -t,  0)), glm::normalize(glm::vec3( 1, -t,  0)),
        glm::normalize(glm::vec3( 0, -1,  t)), glm::normalize(glm::vec3( 0,  1,  t)),
        glm::normalize(glm::vec3( 0, -1, -t)), glm::normalize(glm::vec3( 0,  1, -t)),
        glm::normalize(glm::vec3( t,  0, -1)), glm::normalize(glm::vec3( t,  0,  1)),
        glm::normalize(glm::vec3(-t,  0, -1)), glm::normalize(glm::vec3(-t,  0,  1))
    };

    std::vector<unsigned int> base_indices = {
        0, 11, 5,   0, 5, 1,    0, 1, 7,    0, 7, 10,   0, 10, 11,
        1, 5, 9,    5, 11, 4,   11, 10, 2,  10, 7, 6,   7, 1, 8,
        3, 9, 4,    3, 4, 2,    3, 2, 6,    3, 6, 8,    3, 8, 9,
        4, 9, 5,    2, 4, 11,   6, 2, 10,   8, 6, 7,    9, 8, 1
    };

    // 2. Perform subdivision iterations
    for (int i = 0; i < subdivisions_level; i++) {
        std::vector<unsigned int> new_indices;
        std::map<std::pair<unsigned int, unsigned int>, unsigned int> midpoint_cache;
        
        for (size_t j = 0; j < base_indices.size(); j += 3) {
            unsigned int i1 = base_indices[j], i2 = base_indices[j+1], i3 = base_indices[j+2];
            unsigned int im12 = get_midpoint_index(i1, i2, base_pos, midpoint_cache);
            unsigned int im23 = get_midpoint_index(i2, i3, base_pos, midpoint_cache);
            unsigned int im31 = get_midpoint_index(i3, i1, base_pos, midpoint_cache);
            
            // Subdivide one triangle into four smaller ones
            new_indices.insert(new_indices.end(), {
                i1, im12, im31, 
                im12, i2, im23, 
                im31, im23, i3, 
                im12, im23, im31
            });
        }
        base_indices = new_indices;
    }

    // 3. Generate random phases for procedural organic noise
    std::mt19937 gen(0); // Fixed seed for deterministic visual output
    std::uniform_real_distribution<float> dis(0.0f, 2.0f * 3.1415926535f);
    std::vector<float> base_phases(base_pos.size());
    for (size_t i = 0; i < base_pos.size(); i++) base_phases[i] = dis(gen);

    // 4. Calculate smoothed normals
    std::vector<glm::vec3> base_normals(base_pos.size(), glm::vec3(0.0f));
    for (size_t i = 0; i < base_indices.size(); i += 3) {
        glm::vec3 v1 = base_pos[base_indices[i]];
        glm::vec3 v2 = base_pos[base_indices[i+1]];
        glm::vec3 v3 = base_pos[base_indices[i+2]];
        glm::vec3 normal = glm::normalize(glm::cross(v2 - v1, v3 - v1));
        base_normals[base_indices[i]] += normal; 
        base_normals[base_indices[i+1]] += normal; 
        base_normals[base_indices[i+2]] += normal;
    }
    for (auto& n : base_normals) n = glm::normalize(n);

    // ========================================================================
    // 5. BARYCENTRIC UNROLLING (Anti-Z-Fighting Technique)
    // We duplicate shared vertices so each triangle is structurally independent.
    // This allows the vertex shader to assign (1,0,0), (0,1,0), (0,0,1) coordinates
    // to the corners, enabling pixel-perfect wireframes via the fragment shader.
    // ========================================================================
    vertices.reserve(base_indices.size());
    indices.reserve(base_indices.size());

    const glm::vec3 bary_coords[3] = { 
        glm::vec3(1.0f, 0.0f, 0.0f), 
        glm::vec3(0.0f, 1.0f, 0.0f), 
        glm::vec3(0.0f, 0.0f, 1.0f) 
    };

    for (size_t i = 0; i < base_indices.size(); i++) {
        unsigned int orig_idx = base_indices[i];
        Vertex v;
        v.position = base_pos[orig_idx];
        v.phase = base_phases[orig_idx];
        v.normal = base_normals[orig_idx];
        v.barycentric = bary_coords[i % 3]; 
        
        vertices.push_back(v);
        indices.push_back(static_cast<unsigned int>(i)); // Linear layout
    }
}

// ==============================================================================
// PLUGIN LIFECYCLE
// ==============================================================================

void IcoSphereEffect::update_effect_scaling() {
    // Keep deformations proportionate when the base sphere is scaled down
    scaled_oscill_amp = oscill_amp * sphere_scale;
    scaled_wave_amp = wave_amp * sphere_scale;
    scaled_twist_amp = twist_amp * sphere_scale;
    scaled_pulse_amp = pulse_amp * sphere_scale;
    scaled_noise_amp = noise_amp * sphere_scale;
}

bool IcoSphereEffect::initialize(ICoreContext* core, uint32_t width, uint32_t height) {
    if (program != 0) return true; // Prevent hot-reload duplication

    // 1. Initialize Kinematic physics (Connects to Global Camera & Inertia)
    init_kinematics(core);

    // 2. Bind audio telemetry from the Wayland daemon
    p_audio_bass = core->get_blackboard()->bind_float("audio.bass");
    p_audio_mid = core->get_blackboard()->bind_float("audio.mid");
    p_audio_treble = core->get_blackboard()->bind_float("audio.treble");
    p_audio_bands = core->get_blackboard()->bind_float_array("audio.bands", 64);

    // 3. Compile Shaders
    if (!reload_shader_program()) return false;

    // 4. Generate geometry and upload to GPU
    generate_icosphere(subdivisions);
    
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);

    update_buffers();
    return true;
}

void IcoSphereEffect::on_cleanup() {
    if (program) glDeleteProgram(program);
    if (bloom_blur_program) glDeleteProgram(bloom_blur_program);
    if (bloom_final_program) glDeleteProgram(bloom_final_program);
    
    if (vao) glDeleteVertexArrays(1, &vao);
    if (vbo) glDeleteBuffers(1, &vbo);
    if (ebo) glDeleteBuffers(1, &ebo);
    
    if (matcap_texture_id) glDeleteTextures(1, &matcap_texture_id);
    destroy_bloom_resources();

    program = bloom_blur_program = bloom_final_program = 0;
    vao = vbo = ebo = matcap_texture_id = 0;
}

// ==============================================================================
// SHADER MANAGEMENT
// ==============================================================================

void IcoSphereEffect::fetch_uniform_locations() {
    if (!program) return;
    
    u_model = glGetUniformLocation(program, "model");
    u_view = glGetUniformLocation(program, "view");
    u_projection = glGetUniformLocation(program, "projection");
    u_time = glGetUniformLocation(program, "time");
    
    u_lightColor = glGetUniformLocation(program, "lightColor");
    u_lightPos = glGetUniformLocation(program, "lightPos");
    u_viewPos = glGetUniformLocation(program, "viewPos");

    u_wireframe_color = glGetUniformLocation(program, "wireframe_color");
    u_object_color = glGetUniformLocation(program, "object_color");
    u_is_wireframe_pass = glGetUniformLocation(program, "is_wireframe_pass");

    u_oscill_amp = glGetUniformLocation(program, "oscill_amp");
    u_oscill_freq = glGetUniformLocation(program, "oscill_freq");
    u_wave_amp = glGetUniformLocation(program, "wave_amp");
    u_wave_freq = glGetUniformLocation(program, "wave_freq");
    u_twist_amp = glGetUniformLocation(program, "twist_amp");
    u_pulse_amp = glGetUniformLocation(program, "pulse_amp");
    u_noise_amp = glGetUniformLocation(program, "noise_amp");
    u_sphere_scale = glGetUniformLocation(program, "sphere_scale");

    u_audio_bass = glGetUniformLocation(program, "audio_bass");
    u_audio_mid = glGetUniformLocation(program, "audio_mid");
    u_audio_treble = glGetUniformLocation(program, "audio_treble");
    u_audio_bands = glGetUniformLocation(program, "audio_bands");
    u_shockwaves = glGetUniformLocation(program, "u_shockwaves"); 

    u_use_matcap = glGetUniformLocation(program, "u_use_matcap");
    u_matcap_tex = glGetUniformLocation(program, "u_matcap_tex");
    u_use_instancing = glGetUniformLocation(program, "u_use_instancing");
    u_inner_scale = glGetUniformLocation(program, "u_inner_scale");
    u_outer_scale = glGetUniformLocation(program, "u_outer_scale");
}

bool IcoSphereEffect::reload_shader_program() {
    std::string vert_src = shader_utils::load_shader_source(m_core, get_name(), active_shader + "_vert.glsl");
    std::string frag_src = shader_utils::load_shader_source(m_core, get_name(), active_shader + "_frag.glsl");
    
    if (vert_src.empty() || frag_src.empty()) return false;
    
    GLuint new_program = shader_utils::create_shader_program(vert_src, frag_src);
    if (!new_program) return false;
    
    if (program != 0) glDeleteProgram(program);
    program = new_program;
    fetch_uniform_locations(); 
    return true;
}

void IcoSphereEffect::update_buffers() {
    if (vao == 0) return;
    
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(), GL_STATIC_DRAW);
    
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

    // Memory layout is tightly packed to maximize GPU cache hits
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, position));
    glEnableVertexAttribArray(0);
    
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, phase));
    glEnableVertexAttribArray(1);
    
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));
    glEnableVertexAttribArray(2);
    
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, barycentric));
    glEnableVertexAttribArray(3);
    
    glBindVertexArray(0);
}

// ==============================================================================
// ADVANCED FEATURES (Shockwaves, MatCap, Bloom)
// ==============================================================================

void IcoSphereEffect::trigger_shockwave(const glm::vec3& hit_point) {
    // Inject coordinates into a ring buffer. The shader handles the ripple effect.
    shockwaves[current_shockwave_idx] = glm::vec4(glm::normalize(hit_point), 0.0f);
    current_shockwave_idx = (current_shockwave_idx + 1) % 4;
}

void IcoSphereEffect::update_shockwaves(float dt) {
    for (auto& wave : shockwaves) {
        if (wave.w < 5.0f) wave.w += dt; // W is time since impact
    }
}

void IcoSphereEffect::load_matcap_from_file(const std::string& path) {
    if (path.empty() || path == matcap_filename) return;

    std::string full_path = std::string(getenv("HOME")) + "/.config/interactive-wallpaper/textures/" + path;
    int width, height, channels;
    
    // OpenGL expects textures to be loaded bottom-to-top
    stbi_set_flip_vertically_on_load(true);
    unsigned char* data = stbi_load(full_path.c_str(), &width, &height, &channels, 0);

    if (!data) {
        std::cerr << "[MatCap] Error loading texture: " << full_path << std::endl;
        use_matcap = false;
        return;
    }

    if (matcap_texture_id == 0) glGenTextures(1, &matcap_texture_id);
    
    glBindTexture(GL_TEXTURE_2D, matcap_texture_id);
    GLenum format = (channels == 4) ? GL_RGBA : GL_RGB;
    glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
    
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    glBindTexture(GL_TEXTURE_2D, 0);
    stbi_image_free(data);

    matcap_filename = path;
    use_matcap = true;
}

bool IcoSphereEffect::init_bloom_resources(uint32_t width, uint32_t height) {
    // 1/4 Resolution downsampling for cheap neon glows. Saves immense GPU power.
    uint32_t bloom_w = std::max(1u, width / 4);
    uint32_t bloom_h = std::max(1u, height / 4);

    if (bloom.initialized && bloom.width == bloom_w && bloom.height == bloom_h) return true;
    destroy_bloom_resources();

    bloom.width = bloom_w;
    bloom.height = bloom_h;

    // Setup High-Res Scene Capture FBO
    glGenFramebuffers(1, &bloom.fbo_scene);
    glBindFramebuffer(GL_FRAMEBUFFER, bloom.fbo_scene);

    glGenTextures(1, &bloom.tex_scene_color);
    glBindTexture(GL_TEXTURE_2D, bloom.tex_scene_color);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, bloom.tex_scene_color, 0);

    glGenRenderbuffers(1, &bloom.rbo_depth);
    glBindRenderbuffer(GL_RENDERBUFFER, bloom.rbo_depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, bloom.rbo_depth);

    // Setup Low-Res Ping-Pong FBOs for Gaussian Blur
    glGenFramebuffers(2, bloom.fbo_pingpong);
    glGenTextures(2, bloom.tex_pingpong);
    for (int i = 0; i < 2; i++) {
        glBindFramebuffer(GL_FRAMEBUFFER, bloom.fbo_pingpong[i]);
        glBindTexture(GL_TEXTURE_2D, bloom.tex_pingpong[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, bloom_w, bloom_h, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, bloom.tex_pingpong[i], 0);
    }

    // Hardcoded shaders to prevent file spam
    const char* vs_src = "#version 300 es\n out vec2 UV;\n void main() { float x = float((gl_VertexID & 1) << 2); float y = float((gl_VertexID & 2) << 1); UV = vec2(x * 0.5, y * 0.5); gl_Position = vec4(x - 1.0, y - 1.0, 0.0, 1.0); }";
    const char* fs_blur_src = "#version 300 es\n precision mediump float;\n in vec2 UV;\n uniform sampler2D image;\n uniform bool horizontal;\n uniform vec2 tex_offset;\n out vec4 FragColor;\n void main() { float weight[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216); vec3 result = texture(image, UV).rgb * weight[0]; if(horizontal) { for(int i=1; i<5; ++i) { result += texture(image, UV + vec2(tex_offset.x * float(i), 0.0)).rgb * weight[i]; result += texture(image, UV - vec2(tex_offset.x * float(i), 0.0)).rgb * weight[i]; } } else { for(int i=1; i<5; ++i) { result += texture(image, UV + vec2(0.0, tex_offset.y * float(i))).rgb * weight[i]; result += texture(image, UV - vec2(0.0, tex_offset.y * float(i))).rgb * weight[i]; } } FragColor = vec4(result, 1.0); }";
    const char* fs_final_src = "#version 300 es\n precision mediump float;\n in vec2 UV;\n uniform sampler2D scene;\n uniform sampler2D bloom;\n uniform float intensity;\n out vec4 FragColor;\n void main() { vec3 hdr = texture(scene, UV).rgb; vec3 glow = texture(bloom, UV).rgb; FragColor = vec4(hdr + glow * intensity, 1.0); }";

    if (!bloom_blur_program) bloom_blur_program = shader_utils::create_shader_program(vs_src, fs_blur_src);
    if (!bloom_final_program) bloom_final_program = shader_utils::create_shader_program(vs_src, fs_final_src);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    bloom.initialized = true;
    return true;
}

void IcoSphereEffect::destroy_bloom_resources() {
    if (!bloom.initialized) return;
    glDeleteFramebuffers(1, &bloom.fbo_scene);
    glDeleteTextures(1, &bloom.tex_scene_color);
    glDeleteRenderbuffers(1, &bloom.rbo_depth);
    glDeleteFramebuffers(2, bloom.fbo_pingpong);
    glDeleteTextures(2, bloom.tex_pingpong);
    bloom.initialized = false;
}

void IcoSphereEffect::render_bloom_postprocess(uint32_t width, uint32_t height) {
    glDisable(GL_DEPTH_TEST);
    glUseProgram(bloom_blur_program);
    glUniform2f(glGetUniformLocation(bloom_blur_program, "tex_offset"), 1.0f / bloom.width, 1.0f / bloom.height);

    bool horizontal = true, first_iteration = true;
    int amount = 10; // High quality blur
    glViewport(0, 0, bloom.width, bloom.height);

    for (int i = 0; i < amount; i++) {
        glBindFramebuffer(GL_FRAMEBUFFER, bloom.fbo_pingpong[horizontal]);
        glUniform1i(glGetUniformLocation(bloom_blur_program, "horizontal"), horizontal);
        glBindTexture(GL_TEXTURE_2D, first_iteration ? bloom.tex_scene_color : bloom.tex_pingpong[!horizontal]);
        glDrawArrays(GL_TRIANGLES, 0, 3); // Zero-VBO fullscreen triangle
        horizontal = !horizontal;
        if (first_iteration) first_iteration = false;
    }

    // Send final merged result back to the host Wayland engine
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width, height);
    glUseProgram(bloom_final_program);
    glUniform1f(glGetUniformLocation(bloom_final_program, "intensity"), bloom_intensity);
    
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, bloom.tex_scene_color);
    glUniform1i(glGetUniformLocation(bloom_final_program, "scene"), 0);
    
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, bloom.tex_pingpong[!horizontal]);
    glUniform1i(glGetUniformLocation(bloom_final_program, "bloom"), 1);

    glDrawArrays(GL_TRIANGLES, 0, 3);
    glActiveTexture(GL_TEXTURE0);
}

// ==============================================================================
// MAIN RENDER PIPELINE
// ==============================================================================

void IcoSphereEffect::render(uint32_t width, uint32_t height, float dt) {
    if (needs_shader_reload) { reload_shader_program(); needs_shader_reload = false; }
    if (!program || !vao) return; 
    if (needs_regeneration) { generate_icosphere(subdivisions); update_buffers(); needs_regeneration = false; }

    // --- 1. KINEMATICS UPDATE ---
    // Reads rotation forces from Lua and updates matrices dynamically
    float aspect = (height > 0) ? (float)width / (float)height : 1.0f;
    update_kinematics(dt, aspect);

    update_shockwaves(dt);
    time += dt;

    // --- 2. SETUP RENDER TARGET ---
    if (bloom_intensity > 0.0f) {
        init_bloom_resources(width, height);
        glBindFramebuffer(GL_FRAMEBUFFER, bloom.fbo_scene);
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    glEnable(GL_DEPTH_TEST);
    glViewport(0, 0, width, height);
    
    // Clear only if this is the background layer (controlled via C++ Wayland Core)
    glClearColor(background_color.r, background_color.g, background_color.b, 1.0f);
    // Note: glClear is called by the compositor, but we clear here if we are rendering into our own Bloom FBO
    if (bloom_intensity > 0.0f) glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glUseProgram(program);
    
    // --- 3. UPLOAD UNIFORMS ---
    // Matrices computed by the Kinematic base class
    glUniformMatrix4fv(u_model, 1, GL_FALSE, &model_matrix[0][0]);
    glUniformMatrix4fv(u_view, 1, GL_FALSE, &view_matrix[0][0]);
    glUniformMatrix4fv(u_projection, 1, GL_FALSE, &proj_matrix[0][0]);
    glUniform3f(u_viewPos, current_view_pos.x, current_view_pos.y, current_view_pos.z);
    
    glUniform1f(u_time, time);
    glUniform3fv(u_wireframe_color, 1, &wireframe_color[0]);
    glUniform3fv(u_object_color, 1, &object_color[0]);
    glUniform1i(u_is_wireframe_pass, wireframe_mode ? 1 : 0);
    glUniform1f(u_sphere_scale, sphere_scale);
    
    glUniform1f(u_oscill_amp, scaled_oscill_amp); glUniform1f(u_oscill_freq, oscill_freq);
    glUniform1f(u_wave_amp, scaled_wave_amp);     glUniform1f(u_wave_freq, wave_freq);
    glUniform1f(u_twist_amp, scaled_twist_amp);   glUniform1f(u_pulse_amp, scaled_pulse_amp);
    glUniform1f(u_noise_amp, scaled_noise_amp);

    glUniform1f(u_audio_bass, p_audio_bass ? *p_audio_bass : 0.0f);
    glUniform1f(u_audio_mid, p_audio_mid ? *p_audio_mid : 0.0f);
    glUniform1f(u_audio_treble, p_audio_treble ? *p_audio_treble : 0.0f);
    if (p_audio_bands) glUniform1fv(u_audio_bands, 64, p_audio_bands);
    if (u_shockwaves != -1) glUniform4fv(u_shockwaves, 4, &shockwaves[0][0]);

    glUniform1i(u_use_matcap, use_matcap ? 1 : 0);
    if (use_matcap && matcap_texture_id > 0) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, matcap_texture_id);
        glUniform1i(u_matcap_tex, 0);
    }

    // --- 4. HARDWARE DRAW CALLS ---
    glBindVertexArray(vao);
    if (use_instancing) {
        // Draw two spheres (Inner core & Outer wireframe cage) in 1 draw call
        glUniform1i(u_use_instancing, 1);
        glUniform1f(u_inner_scale, inner_scale);
        glUniform1f(u_outer_scale, outer_scale);
        glDrawElementsInstanced(GL_TRIANGLES, indices.size(), GL_UNSIGNED_INT, 0, 2);
    } else {
        glUniform1i(u_use_instancing, 0);
        glDrawElements(GL_TRIANGLES, indices.size(), GL_UNSIGNED_INT, 0);
    }
    glBindVertexArray(0);

    // --- 5. FINALIZE ---
    if (bloom_intensity > 0.0f) {
        render_bloom_postprocess(width, height);
    }
    glDisable(GL_DEPTH_TEST);
}

// ==============================================================================
// GETTERS & SETTERS (Hot-Reload Adapters)
// ==============================================================================

void IcoSphereEffect::set_subdivisions(int value) { 
    int new_subdivisions = std::clamp(value, 0, 6);
    if (new_subdivisions != subdivisions) {
        subdivisions = new_subdivisions;
        needs_regeneration = true;
    }
}

void IcoSphereEffect::set_sphere_scale(float scale) { 
    if (std::abs(sphere_scale - scale) > 0.001f) {
        sphere_scale = scale;
        update_effect_scaling();
    }
}

// ==============================================================================
// PLUGIN EXPORT API (Lua Bindings)
// ==============================================================================

class IcoSphereEffectPlugin : public IcoSphereEffect {
public:
    const char* get_name() const override { return "Icosahedron Sphere"; }

    std::vector<EffectParameter> get_parameters() const override {
        // 1. Inherit standard kinematics physics parameters (Offset, Rotation force, Friction)
        auto params = get_kinematic_params();

        // 2. Append plugin-specific visual parameters
        params.push_back({"shader_theme", "Shader variation name", active_shader});
        params.push_back({"wireframe_mode", "Render as a wireframe", wireframe_mode});
        params.push_back({"subdivisions", "Level of sphere detail (0-6)", subdivisions});
        params.push_back({"sphere_scale", "Overall size of the sphere", sphere_scale});
        params.push_back({"background_color", "Background clear color", background_color});
        params.push_back({"wireframe_color", "Color of the wireframe lines", wireframe_color});
        params.push_back({"object_color", "Color of the solid sphere surface", object_color});
        params.push_back({"oscill_amp", "Oscillation Amplitude", oscill_amp});
        params.push_back({"oscill_freq", "Oscillation Frequency", oscill_freq});
        params.push_back({"wave_amp", "Wave Amplitude", wave_amp});
        params.push_back({"wave_freq", "Wave Frequency", wave_freq});
        params.push_back({"twist_amp", "Twist Amplitude", twist_amp});
        params.push_back({"pulse_amp", "Pulse Amplitude", pulse_amp});
        params.push_back({"noise_amp", "Noise Amplitude", noise_amp});
        params.push_back({"bloom_intensity", "Neon glow intensity (0.0 = off, 1.0+ = strong)", bloom_intensity});
        params.push_back({"matcap_texture", "Filename of MatCap texture in ~/.config/.../textures/", matcap_filename});
        params.push_back({"use_instancing", "Draw inner core and outer cage in 1 call", use_instancing});
        params.push_back({"inner_scale", "Scale of the inner instanced core", inner_scale});
        params.push_back({"outer_scale", "Scale of the outer instanced cage", outer_scale});

        register_standard_params(params); 

        
        return params;
    }

    void set_parameter(const std::string& name, const EffectParameterValue& value) override {
        // Compatibility stubs for legacy Lua presets 
        // (Prevent error spam when loading old scenes)
        if (apply_standard_param(name, value)) return;

        if (name == "constant_rotation_speed" || name == "rotation_decay" || 
            name == "min_rotation_speed" || name == "max_rotation_speed") return;

        // Route physics parameters to the base class handler
        if (set_kinematic_param(name, value)) return;

        try {
            if (name == "shader_theme") {
                std::string new_theme = std::get<std::string>(value);
                if (new_theme != active_shader) { active_shader = new_theme; needs_shader_reload = true; }
            }
            else if (name == "wireframe_mode")   { set_wireframe_mode(std::get<bool>(value)); }
            else if (name == "subdivisions")     { set_subdivisions(std::get<int>(value)); }
            else if (name == "sphere_scale")     { set_sphere_scale(std::get<float>(value)); }
            else if (name == "background_color") { set_background_color(std::get<glm::vec3>(value)); }
            else if (name == "wireframe_color")  { set_wireframe_color(std::get<glm::vec3>(value)); }
            else if (name == "object_color")     { set_object_color(std::get<glm::vec3>(value)); }
            else if (name == "oscill_amp")       { set_oscill_amp(std::get<float>(value)); }
            else if (name == "oscill_freq")      { set_oscill_freq(std::get<float>(value)); }
            else if (name == "wave_amp")         { set_wave_amp(std::get<float>(value)); }
            else if (name == "wave_freq")        { set_wave_freq(std::get<float>(value)); }
            else if (name == "twist_amp")        { set_twist_amp(std::get<float>(value)); }
            else if (name == "pulse_amp")        { set_pulse_amp(std::get<float>(value)); }
            else if (name == "noise_amp")        { set_noise_amp(std::get<float>(value)); }
            else if (name == "bloom_intensity")  { set_bloom_intensity(std::get<float>(value)); }
            else if (name == "matcap_texture")   { load_matcap_from_file(std::get<std::string>(value)); }
            else if (name == "use_instancing")   { set_use_instancing(std::get<bool>(value)); }
            else if (name == "inner_scale")      { set_inner_scale(std::get<float>(value)); }
            else if (name == "outer_scale")      { set_outer_scale(std::get<float>(value)); }
        } catch (const std::bad_variant_access& e) {
            std::cerr << "[IcoSphere] Warning: Type mismatch for parameter '" << name << "'. " << e.what() << std::endl;
        }
    }
};

// ==============================================================================
// C-ABI EXPORT (Dynamic Library Boundary)
// ==============================================================================
extern "C" {
    uint32_t get_abi_version() { return SHADER_DESK_ABI_VERSION; }
    IWallpaperEffectABI* create_effect() { return new IcoSphereEffectPlugin(); }
    void destroy_effect(IWallpaperEffectABI* effect) { delete static_cast<WallpaperEffect*>(effect); }
}