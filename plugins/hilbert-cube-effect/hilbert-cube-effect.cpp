#include "hilbert-cube-effect.hpp"
#include <shader-desk/shader-utils.hpp>
#include <iostream>
#include <cmath>
#include <algorithm>

// ==============================================================================
// CONSTRUCTOR & DESTRUCTOR
// ==============================================================================
HilbertCubeEffect::HilbertCubeEffect() {}

HilbertCubeEffect::~HilbertCubeEffect() {
    on_cleanup();
}

// ==============================================================================
// 3D HILBERT CURVE GENERATION ALGORITHM
// ==============================================================================
// This is a recursive algorithm that generates a continuous space-filling curve 
// inside a 3D volume. It maps a 1D sequence of points into a 3D grid.
void HilbertCubeEffect::hilbert3D(const glm::vec3& start, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c,
                                  int order, std::vector<glm::vec3>& vertices)
{
    // Base case: The lowest level of recursion generates the 8 corners of a sub-cube.
    if (order <= 1) {
        vertices.push_back(start);
        vertices.push_back(start + a);
        vertices.push_back(start + a + b);
        vertices.push_back(start + b);
        vertices.push_back(start + b + c);
        vertices.push_back(start + a + b + c);
        vertices.push_back(start + a + c);
        vertices.push_back(start + c);
    } else {
        // Recursive step: Divide the current volume into 8 smaller sub-cubes.
        // We use a 1/3 scale factor here (instead of standard 1/2) to create 
        // a specific visual gap between the fractal segments.
        const glm::vec3 a_s = a / 3.0f;
        const glm::vec3 b_s = b / 3.0f;
        const glm::vec3 c_s = c / 3.0f;

        // Pre-calculate the starting coordinate for each of the 8 sub-octants.
        const glm::vec3 p0 = start;
        const glm::vec3 p1 = start + a * (2.0f / 3.0f);
        const glm::vec3 p2 = start + (a + b) * (2.0f / 3.0f);
        const glm::vec3 p3 = start + b + a_s;
        const glm::vec3 p4 = start + b + a_s + c * (2.0f / 3.0f);
        const glm::vec3 p5 = start + b + c + a_s * 2.0f;
        const glm::vec3 p6 = start + c + b_s + a_s * 2.0f;
        const glm::vec3 p7 = start + c + a_s;

        // Recursively call hilbert3D, transforming (rotating/reflecting) the axes 
        // to ensure the curve remains continuous without intersecting itself.
        hilbert3D(p0,  b_s,  c_s,  a_s, order - 1, vertices);
        hilbert3D(p1,  c_s,  a_s,  b_s, order - 1, vertices);
        hilbert3D(p2,  c_s,  a_s,  b_s, order - 1, vertices);
        hilbert3D(p3, -a_s, -b_s,  c_s, order - 1, vertices);
        hilbert3D(p4, -a_s, -b_s,  c_s, order - 1, vertices);
        hilbert3D(p5, -c_s,  a_s, -b_s, order - 1, vertices);
        hilbert3D(p6, -c_s,  a_s, -b_s, order - 1, vertices);
        hilbert3D(p7,  b_s, -c_s, -a_s, order - 1, vertices);
    }
}

void HilbertCubeEffect::generate_hilbert_curve() {
    std::vector<glm::vec3> vertices;
    float size = 1.0f;
    glm::vec3 start_pos(-size / 2.0f, -size / 2.0f, -size / 2.0f);
    glm::vec3 a(size, 0, 0), b(0, size, 0), c(0, 0, size);
    
    // Execute the recursive generation
    hilbert3D(start_pos, a, b, c, hilbert_order, vertices);

    curve_vertex_count = static_cast<GLsizei>(vertices.size());

    // Upload geometry to the GPU
    glGenVertexArrays(1, &curve_vao);
    glGenBuffers(1, &curve_vbo);

    glBindVertexArray(curve_vao);
    glBindBuffer(GL_ARRAY_BUFFER, curve_vbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(glm::vec3), vertices.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);
    glEnableVertexAttribArray(0);
    
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void HilbertCubeEffect::generate_cube_outline() {
    float s = 0.6f; // Slightly larger than the curve to act as a bounding box
    std::vector<glm::vec3> vertices = {
        {-s, -s, -s}, {s, -s, -s}, {s, s, -s}, {-s, s, -s},
        {-s, -s,  s}, {s, -s,  s}, {s, s,  s}, {-s, s,  s}
    };
    
    // Line indices connecting the 8 corners of the cube
    std::vector<unsigned int> indices = {
        0, 1, 1, 2, 2, 3, 3, 0, // Bottom face
        4, 5, 5, 6, 6, 7, 7, 4, // Top face
        0, 4, 1, 5, 2, 6, 3, 7  // Side edges
    };

    // Upload geometry to the GPU
    glGenVertexArrays(1, &cube_vao);
    glGenBuffers(1, &cube_vbo);
    glGenBuffers(1, &cube_ebo);

    glBindVertexArray(cube_vao);
    
    glBindBuffer(GL_ARRAY_BUFFER, cube_vbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(glm::vec3), vertices.data(), GL_STATIC_DRAW);
    
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, cube_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);
    glEnableVertexAttribArray(0);
    
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

// ==============================================================================
// PLUGIN INITIALIZATION
// ==============================================================================
bool HilbertCubeEffect::initialize(ICoreContext* core, uint32_t width, uint32_t height) {
    // Hot-reload protection: Do not re-initialize if the program is already valid.
    if (program) return true;

    // 1. INITIALIZE KINEMATICS (From Base Class)
    // This connects our plugin to the BlackBoard's Zero-Latency Global Camera 
    // and sets up the internal physics state (inertia, velocities).
    init_kinematics(core);

    // 2. LOAD SHADERS
    std::string vert_src = shader_utils::load_shader_source(core, get_name(), "cube_vert.glsl");
    std::string frag_src = shader_utils::load_shader_source(core, get_name(), "cube_frag.glsl");
    if (vert_src.empty() || frag_src.empty()) return false;

    // 3. COMPILE SHADER PROGRAM
    program = shader_utils::create_shader_program(vert_src, frag_src);
    if (!program) return false;

    // 4. CACHE UNIFORM LOCATIONS
    u_model = glGetUniformLocation(program, "model");
    u_view = glGetUniformLocation(program, "view");
    u_projection = glGetUniformLocation(program, "projection");
    u_line_color = glGetUniformLocation(program, "line_color");

    return true;
}

// ==============================================================================
// RENDER PIPELINE (HOT PATH)
// Executed at monitor refresh rate (e.g., 144Hz). Must be Zero-Allocation.
// ==============================================================================
void HilbertCubeEffect::render(uint32_t width, uint32_t height, float dt) {
    // 1. DEFERRED GEOMETRY REGENERATION
    // CPU geometry generation is computationally heavy. We only rebuild the VBOs 
    // when Lua explicitly changes the `hilbert_order`. This prevents frame drops.
    if (needs_regeneration) {
        if (curve_vao) glDeleteVertexArrays(1, &curve_vao);
        if (curve_vbo) glDeleteBuffers(1, &curve_vbo);
        if (cube_vao) glDeleteVertexArrays(1, &cube_vao);
        if (cube_vbo) glDeleteBuffers(1, &cube_vbo);
        if (cube_ebo) glDeleteBuffers(1, &cube_ebo);

        generate_hilbert_curve();
        generate_cube_outline();
        needs_regeneration = false;
    }

    // 2. KINEMATIC PHYSICS & GLOBAL CAMERA
    // Computes rotational momentum, inertia friction, and transforms the 
    // object based on the global Wayland scene camera coordinates.
    float aspect = (height > 0) ? static_cast<float>(width) / static_cast<float>(height) : 1.0f;
    update_kinematics(dt, aspect); 
    
    // ========================================================================
    // 3. OBJECT ISOLATION (For Nested Filters & Resolution Scaling)
    // Diverts rendering from the main screen into a local Ping-Pong FBO.
    // If no filters are attached and scale == 1.0, this safely acts as a passthrough.
    // ========================================================================
    auto [target_w, target_h] = begin_scaled_pass(width, height);

    // 4. OPENGL PIPELINE SETUP
    glEnable(GL_DEPTH_TEST);
    glLineWidth(1.0f); // Fallback line thickness (driver dependent)
    glUseProgram(program);
    
    // 5. DISPATCH PRE-CALCULATED MATRICES
    // The MVP matrices were calculated in O(1) time inside update_kinematics()
    glUniformMatrix4fv(u_model, 1, GL_FALSE, &model_matrix[0][0]);
    glUniformMatrix4fv(u_view, 1, GL_FALSE, &view_matrix[0][0]);
    glUniformMatrix4fv(u_projection, 1, GL_FALSE, &proj_matrix[0][0]);
    
    // 6. DRAW THE HILBERT FRACTAL CURVE
    glUniform3fv(u_line_color, 1, &curve_color[0]);
    glBindVertexArray(curve_vao);
    glDrawArrays(GL_LINE_STRIP, 0, curve_vertex_count);

    // 7. DRAW THE BOUNDING WIREFRAME BOX
    if (draw_cube_outline) {
        glUniform3fv(u_line_color, 1, &cube_color[0]);
        glBindVertexArray(cube_vao);
        glDrawElements(GL_LINES, 24, GL_UNSIGNED_INT, 0);
    }
    
    // 8. DEFENSIVE STATE ISOLATION
    // Never trust the compositor or subsequent plugins to reset your OpenGL state.
    glBindVertexArray(0);
    glDisable(GL_DEPTH_TEST);

    // ========================================================================
    // 9. APPLY FILTERS AND COMPOSITE BACK TO SCREEN
    // Passes the local texture through the attached post-processing plugins (filters).
    // Performs hardware Depth Blitting to copy the 3D relief of the cube into 
    // the global Wayland scene, ensuring it occludes other objects correctly.
    // ========================================================================
    end_scaled_pass(dt);
}

void HilbertCubeEffect::on_cleanup() {
    if (program) glDeleteProgram(program);
    if (curve_vao) glDeleteVertexArrays(1, &curve_vao);
    if (curve_vbo) glDeleteBuffers(1, &curve_vbo);
    if (cube_vao) glDeleteVertexArrays(1, &cube_vao);
    if (cube_vbo) glDeleteBuffers(1, &cube_vbo);
    if (cube_ebo) glDeleteBuffers(1, &cube_ebo);
    program = curve_vao = curve_vbo = cube_vao = cube_vbo = cube_ebo = 0;
}

// ==============================================================================
// PLUGIN API INTERFACE
// ==============================================================================

const char* HilbertCubeEffect::get_name() const { 
    return "Hilbert Cube"; 
}

std::vector<EffectParameter> HilbertCubeEffect::get_parameters() const {
    // 1. Fetch the standard kinematics/physics parameters from the base class
    auto params = get_kinematic_params();
    
    // 2. Append the visual parameters specific to this plugin
    params.push_back({"hilbert_order", "Detail of the Hilbert curve (1-5)", hilbert_order});
    params.push_back({"draw_cube_outline", "Draw the cube's wireframe", draw_cube_outline});
    params.push_back({"curve_color", "Color of the Hilbert curve", curve_color});
    params.push_back({"cube_color", "Color of the cube wireframe", cube_color});
    register_standard_params(params); 

    
    return params;
}

void HilbertCubeEffect::set_parameter(const std::string& name, const EffectParameterValue& value) {

    if (apply_standard_param(name, value)) return;

    // 1. Pass the parameter to the Kinematics handler first.
    // If it matches a physics property (like "rotation_speed" or "offset"), 
    // it will be processed and return true, skipping the rest of the function.
    if (set_kinematic_param(name, value)) return;

    // 2. Process local visual parameters
    try {
        if (name == "hilbert_order") {
            int new_order = std::clamp(std::get<int>(value), 1, 5);
            if (new_order != hilbert_order) {
                hilbert_order = new_order;
                needs_regeneration = true; // Trigger deferred geometry update
            }
        } 
        else if (name == "draw_cube_outline") draw_cube_outline = std::get<bool>(value);
        else if (name == "curve_color") curve_color = std::get<glm::vec3>(value);
        else if (name == "cube_color") cube_color = std::get<glm::vec3>(value);
        else {
            std::cerr << "[HilbertCube] Warning: Unknown parameter '" << name << "'." << std::endl;
        }
    } catch (const std::bad_variant_access& e) {
        std::cerr << "[HilbertCube] Warning: Type mismatch for parameter '" << name << "'. " << e.what() << std::endl;
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
        return new HilbertCubeEffect(); 
    }
    
    void destroy_effect(IWallpaperEffectABI* effect) { 
        // Cast back to WallpaperEffect (or KinematicEffect) to ensure virtual destructor is called
        delete static_cast<WallpaperEffect*>(effect); 
    }
}