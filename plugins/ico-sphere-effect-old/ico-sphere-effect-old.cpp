#define GLM_ENABLE_EXPERIMENTAL

#include "ico-sphere-effect-old.hpp"
#include <shader-desk/shader-utils.hpp> 

#include <iostream>
#include <cmath>
#include <algorithm>
#include <random>

// ==============================================================================
// GEOMETRY HELPERS
// ==============================================================================
// Calculates the midpoint index of an edge for icosphere subdivision
unsigned int get_midpoint_index(unsigned int i1, unsigned int i2,
                               std::vector<glm::vec3>& vertices,
                               std::map<std::pair<unsigned int, unsigned int>, unsigned int>& cache) {
    std::pair<unsigned int, unsigned int> edge_key(std::min(i1, i2), std::max(i1, i2));
    auto it = cache.find(edge_key);
    if (it != cache.end()) {
        return it->second;
    }

    glm::vec3 v1 = vertices[i1];
    glm::vec3 v2 = vertices[i2];
    glm::vec3 midpoint = glm::normalize((v1 + v2) * 0.5f);
    unsigned int new_index = vertices.size();
    vertices.push_back(midpoint);
    cache[edge_key] = new_index;
    return new_index;
}

// ==============================================================================
// CONSTRUCTOR & DESTRUCTOR
// ==============================================================================
IcoSphereEffect::IcoSphereEffect() {
    // Add a slight random initial spin to the sphere (Kinematic state)
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> dis(-1.0f, 1.0f);
    angular_velocity = glm::vec3(dis(gen), dis(gen), dis(gen)) * 0.1f;
    
    update_effect_scaling();
}

IcoSphereEffect::~IcoSphereEffect() {
    cleanup();
}

// ==============================================================================
// CPU GEOMETRY GENERATION
// ==============================================================================
void IcoSphereEffect::generate_icosphere(int subdivisions_level) {
    vertices.clear();
    indices.clear();
    phases.clear();
    normals.clear();
    line_indices.clear();
    const float t = (1.0f + std::sqrt(5.0f)) / 2.0f;

    vertices = {
        glm::normalize(glm::vec3(-1,  t,  0)), glm::normalize(glm::vec3( 1,  t,  0)),
        glm::normalize(glm::vec3(-1, -t,  0)), glm::normalize(glm::vec3( 1, -t,  0)),
        glm::normalize(glm::vec3( 0, -1,  t)), glm::normalize(glm::vec3( 0,  1,  t)),
        glm::normalize(glm::vec3( 0, -1, -t)), glm::normalize(glm::vec3( 0,  1, -t)),
        glm::normalize(glm::vec3( t,  0, -1)), glm::normalize(glm::vec3( t,  0,  1)),
        glm::normalize(glm::vec3(-t,  0, -1)), glm::normalize(glm::vec3(-t,  0,  1))
    };
    
    indices = {
        0, 11, 5,   0, 5, 1,    0, 1, 7,    0, 7, 10,   0, 10, 11,
        1, 5, 9,    5, 11, 4,   11, 10, 2,  10, 7, 6,   7, 1, 8,
        3, 9, 4,    3, 4, 2,    3, 2, 6,    3, 6, 8,    3, 8, 9,
        4, 9, 5,    2, 4, 11,   6, 2, 10,   8, 6, 7,    9, 8, 1
    };
    
    for (int i = 0; i < subdivisions_level; i++) {
        std::vector<unsigned int> new_indices;
        std::map<std::pair<unsigned int, unsigned int>, unsigned int> midpoint_cache;

        for (size_t j = 0; j < indices.size(); j += 3) {
            unsigned int i1 = indices[j], i2 = indices[j+1], i3 = indices[j+2];
            unsigned int im12 = get_midpoint_index(i1, i2, vertices, midpoint_cache);
            unsigned int im23 = get_midpoint_index(i2, i3, vertices, midpoint_cache);
            unsigned int im31 = get_midpoint_index(i3, i1, vertices, midpoint_cache);
            new_indices.insert(new_indices.end(), {i1, im12, im31});
            new_indices.insert(new_indices.end(), {im12, i2, im23});
            new_indices.insert(new_indices.end(), {im31, im23, i3});
            new_indices.insert(new_indices.end(), {im12, im23, im31});
        }
        indices = new_indices;
    }

    std::mt19937 gen(0);
    std::uniform_real_distribution<float> dis(0.0f, 2.0f * 3.1415926535f);
    phases.resize(vertices.size());
    for (size_t i = 0; i < vertices.size(); i++) {
        phases[i] = dis(gen);
    }

    normals.assign(vertices.size(), glm::vec3(0.0f));
    for (size_t i = 0; i < indices.size(); i += 3) {
        glm::vec3 v1 = vertices[indices[i]], v2 = vertices[indices[i+1]], v3 = vertices[indices[i+2]];
        glm::vec3 normal = glm::normalize(glm::cross(v2 - v1, v3 - v1));
        normals[indices[i]] += normal;
        normals[indices[i+1]] += normal;
        normals[indices[i+2]] += normal;
    }
    for (auto& normal : normals) {
        normal = glm::normalize(normal);
    }

    line_indices.clear();
    for (size_t i = 0; i < indices.size(); i += 3) {
        unsigned int i1 = indices[i], i2 = indices[i+1], i3 = indices[i+2];
        line_indices.push_back(i1); line_indices.push_back(i2);
        line_indices.push_back(i2); line_indices.push_back(i3);
        line_indices.push_back(i3); line_indices.push_back(i1);
    }
}

void IcoSphereEffect::update_buffers() {
    if (vao == 0) return;

    glBindVertexArray(vao);
    
    std::vector<float> vertex_data;
    vertex_data.reserve(vertices.size() * 7);
    for (size_t i = 0; i < vertices.size(); i++) {
        vertex_data.push_back(vertices[i].x);
        vertex_data.push_back(vertices[i].y);
        vertex_data.push_back(vertices[i].z);
        vertex_data.push_back(phases[i]);
        vertex_data.push_back(normals[i].x);
        vertex_data.push_back(normals[i].y);
        vertex_data.push_back(normals[i].z);
    }
    
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, vertex_data.size() * sizeof(float), vertex_data.data(), GL_STATIC_DRAW);
    
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, line_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, line_indices.size() * sizeof(unsigned int), line_indices.data(), GL_STATIC_DRAW);
    
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)(4 * sizeof(float)));
    glEnableVertexAttribArray(2);
    
    glBindVertexArray(0);
}

void IcoSphereEffect::update_effect_scaling() {
    scaled_oscill_amp = oscill_amp * sphere_scale;
    scaled_wave_amp = wave_amp * sphere_scale;
    scaled_twist_amp = twist_amp * sphere_scale;
    scaled_pulse_amp = pulse_amp * sphere_scale;
    scaled_noise_amp = noise_amp * sphere_scale;
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
    
    u_wireframe_color = glGetUniformLocation(program, "wireframe_color");
    u_object_color = glGetUniformLocation(program, "object_color"); // Fixed binding!
    u_is_wireframe_pass = glGetUniformLocation(program, "is_wireframe_pass");
    
    u_lightColor = glGetUniformLocation(program, "lightColor");
    u_lightPos = glGetUniformLocation(program, "lightPos");
    u_viewPos = glGetUniformLocation(program, "viewPos");

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
}

bool IcoSphereEffect::reload_shader_program() {
    std::cout << "[IcoSphere] Attempting to load shader theme: '" << active_shader << "'" << std::endl;
    
    std::string vert_src = shader_utils::load_shader_source(m_core, get_name(), active_shader + "_vert.glsl");
    std::string frag_src = shader_utils::load_shader_source(m_core, get_name(), active_shader + "_frag.glsl");

    if (vert_src.empty() || frag_src.empty()) {
        std::cerr << "[IcoSphere] Failed to load shader files for theme: " << active_shader << std::endl;
        return false;
    }
    
    GLuint new_program = shader_utils::create_shader_program(vert_src, frag_src);
    if (!new_program) return false;
    
    if (program != 0) glDeleteProgram(program);
    
    program = new_program;
    fetch_uniform_locations(); 
    
    std::cout << "[IcoSphere] Successfully switched to shader theme: '" << active_shader << "'" << std::endl;
    return true;
}

// ==============================================================================
// PLUGIN INITIALIZATION
// ==============================================================================
bool IcoSphereEffect::initialize(ICoreContext* core, uint32_t width, uint32_t height) {
    if (program != 0) return true;

    // 1. INITIALIZE KINEMATICS (Base Class)
    // Connects to the Global Camera and sets up physics.
    init_kinematics(core);

    // 2. BLACKBOARD MEMORY MAPPING (Audio Telemetry)
    p_audio_bass   = core->get_blackboard()->bind_float("audio.bass");
    p_audio_mid    = core->get_blackboard()->bind_float("audio.mid");
    p_audio_treble = core->get_blackboard()->bind_float("audio.treble");
    p_audio_bands  = core->get_blackboard()->bind_float_array("audio.bands", 64);

    // 3. SHADER COMPILATION
    if (!reload_shader_program()) return false; 

    // 4. GEOMETRY GENERATION
    generate_icosphere(subdivisions);
    
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);
    glGenBuffers(1, &line_ebo);
    update_buffers();

    return true;
}

// ==============================================================================
// RENDER PIPELINE
// ==============================================================================
void IcoSphereEffect::render(uint32_t width, uint32_t height, float dt) {
    // 1. DEFERRED HOT-RELOADING & REGENERATION
    if (needs_shader_reload) {
        reload_shader_program();
        needs_shader_reload = false;
    }

    if (needs_regeneration) {
        generate_icosphere(subdivisions);
        update_buffers();
        needs_regeneration = false;
    }

    // 2. KINEMATIC PHYSICS & GLOBAL CAMERA (Base Class)
    float aspect = (height > 0) ? (float)width / (float)height : 1.0f;
    update_kinematics(dt, aspect); 
    
    // 3. OPENGL PIPELINE SETUP
    glEnable(GL_DEPTH_TEST);
    glUseProgram(program);
    
    glUniformMatrix4fv(u_model, 1, GL_FALSE, &model_matrix[0][0]);
    glUniformMatrix4fv(u_view, 1, GL_FALSE, &view_matrix[0][0]);
    glUniformMatrix4fv(u_projection, 1, GL_FALSE, &proj_matrix[0][0]);

    // 4. LIGHTING & VISUAL UNIFORMS
    glUniform3f(u_lightColor, 1.0f, 1.0f, 1.0f); 
    glUniform3f(u_lightPos, 5.0f, 5.0f, 5.0f);   
    glUniform3f(u_viewPos, current_view_pos.x, current_view_pos.y, current_view_pos.z);    
    
    time += dt;
    glUniform1f(u_time, time);
    
    glUniform1f(u_oscill_amp, scaled_oscill_amp);
    glUniform1f(u_oscill_freq, oscill_freq);
    glUniform1f(u_wave_amp, scaled_wave_amp);
    glUniform1f(u_wave_freq, wave_freq);
    glUniform1f(u_twist_amp, scaled_twist_amp);
    glUniform1f(u_pulse_amp, scaled_pulse_amp);
    glUniform1f(u_noise_amp, scaled_noise_amp);
    glUniform1f(u_sphere_scale, sphere_scale);

    // 5. AUDIO TELEMETRY UNIFORMS
    glUniform1f(u_audio_bass, p_audio_bass ? *p_audio_bass : 0.0f);
    glUniform1f(u_audio_mid, p_audio_mid ? *p_audio_mid : 0.0f);
    glUniform1f(u_audio_treble, p_audio_treble ? *p_audio_treble : 0.0f);
    if (p_audio_bands) glUniform1fv(u_audio_bands, 64, p_audio_bands);

    // 6. HARDWARE DRAW CALL
    glBindVertexArray(vao);
    
    if (wireframe_mode) {
        glUniform3f(u_wireframe_color, wireframe_color.r, wireframe_color.g, wireframe_color.b);
        glUniform1i(u_is_wireframe_pass, 1);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, line_ebo);
        glDrawElements(GL_LINES, line_indices.size(), GL_UNSIGNED_INT, 0);
    } else {
        glUniform3f(u_object_color, background_color.r, background_color.g, background_color.b);
        glUniform3f(u_wireframe_color, wireframe_color.r, wireframe_color.g, wireframe_color.b); // Used for rim light
        glUniform1i(u_is_wireframe_pass, 0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        glDrawElements(GL_TRIANGLES, indices.size(), GL_UNSIGNED_INT, 0);
    }
    
    // 7. STATE ISOLATION
    glBindVertexArray(0);
    glDisable(GL_DEPTH_TEST);
}

void IcoSphereEffect::cleanup() {
    if (program) glDeleteProgram(program);
    if (vao) glDeleteVertexArrays(1, &vao);
    if (vbo) glDeleteBuffers(1, &vbo);
    if (ebo) glDeleteBuffers(1, &ebo);
    if (line_ebo) glDeleteBuffers(1, &line_ebo);
    program = vao = vbo = ebo = line_ebo = 0;
}

// ==============================================================================
// PLUGIN API INTERFACE
// ==============================================================================

const char* IcoSphereEffect::get_name() const {
    return "Icosahedron Sphere Old";
}

std::vector<EffectParameter> IcoSphereEffect::get_parameters() const {
    // 1. Fetch physics parameters from the Kinematic base class
    auto params = get_kinematic_params();
    
    // 2. Add visual parameters
    params.push_back({"shader_theme", "Shader variation name", active_shader});
    params.push_back({"wireframe_mode", "Render as a wireframe", wireframe_mode});
    params.push_back({"subdivisions", "Level of sphere detail (0-6)", subdivisions});
    params.push_back({"sphere_scale", "Overall size of the sphere", sphere_scale});
    params.push_back({"oscill_amp", "Oscillation Amplitude", oscill_amp});
    params.push_back({"oscill_freq", "Oscillation Frequency", oscill_freq});
    params.push_back({"wave_amp", "Wave Amplitude", wave_amp});
    params.push_back({"wave_freq", "Wave Frequency", wave_freq});
    params.push_back({"twist_amp", "Twist Amplitude", twist_amp});
    params.push_back({"pulse_amp", "Pulse Amplitude", pulse_amp});
    params.push_back({"noise_amp", "Noise Amplitude", noise_amp});
    params.push_back({"background_color", "Solid surface color", background_color});
    params.push_back({"wireframe_color", "Color of the wireframe lines", wireframe_color});
    
    return params;
}

void IcoSphereEffect::set_parameter(const std::string& name, const EffectParameterValue& value) {
    // 1. Intercept legacy parameters to prevent breaking old presets
    if (name == "constant_rotation_speed") {
        try { rotation_speed = std::get<float>(value); } catch (...) {}
        return;
    }
    if (name == "rotation_decay" || name == "min_rotation_speed" || name == "max_rotation_speed") {
        // Silently ignore legacy variables, Kinematics handles friction automatically
        return;
    }

    // 2. Delegate to the Kinematics handler first
    if (set_kinematic_param(name, value)) return;

    // 3. Process local visual parameters
    try {
        if (name == "shader_theme") {
            std::string new_theme = std::get<std::string>(value);
            if (new_theme != active_shader) {
                active_shader = new_theme;
                needs_shader_reload = true; 
            }
        }
        else if (name == "wireframe_mode") wireframe_mode = std::get<bool>(value);
        else if (name == "subdivisions") {
            int val = std::clamp(std::get<int>(value), 0, 6);
            if (val != subdivisions) {
                subdivisions = val;
                needs_regeneration = true;
            }
        }
        else if (name == "sphere_scale") { 
            sphere_scale = std::get<float>(value); 
            update_effect_scaling(); 
        }
        else if (name == "oscill_amp")   { oscill_amp = std::get<float>(value); update_effect_scaling(); }
        else if (name == "oscill_freq")  { oscill_freq = std::get<float>(value); }
        else if (name == "wave_amp")     { wave_amp = std::get<float>(value); update_effect_scaling(); }
        else if (name == "wave_freq")    { wave_freq = std::get<float>(value); }
        else if (name == "twist_amp")    { twist_amp = std::get<float>(value); update_effect_scaling(); }
        else if (name == "pulse_amp")    { pulse_amp = std::get<float>(value); update_effect_scaling(); }
        else if (name == "noise_amp")    { noise_amp = std::get<float>(value); update_effect_scaling(); }
        else if (name == "background_color") background_color = std::get<glm::vec3>(value);
        else if (name == "wireframe_color")  wireframe_color = std::get<glm::vec3>(value);
        else {
             std::cerr << "[IcoSphere] Warning: Unknown parameter '" << name << "'." << std::endl;
        }
    } catch (const std::bad_variant_access& e) {
        std::cerr << "[IcoSphere] Warning: Type mismatch for parameter '" << name << "'. " << e.what() << std::endl;
    }
}

// ==============================================================================
// C-ABI EXPORT FUNCTIONS
// ==============================================================================
extern "C" {
    uint32_t get_abi_version() { 
        return SHADER_DESK_ABI_VERSION; 
    }
    
    IWallpaperEffectABI* create_effect() {
        return new IcoSphereEffect(); 
    }
    
    void destroy_effect(IWallpaperEffectABI* effect) {
        delete static_cast<WallpaperEffect*>(effect);
    }
}