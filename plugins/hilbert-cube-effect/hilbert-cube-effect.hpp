#ifndef HILBERT_CUBE_EFFECT_HPP
#define HILBERT_CUBE_EFFECT_HPP

// Include the new Kinematic SDK base class.
// This automatically provides standard 3D physics (inertia, rotational impulses),
// center of mass (pivot) adjustments, and seamless integration with the 
// Zero-Latency Global Camera controlled by Lua.
#include <shader-desk/kinematic-effect.hpp>

#include <GLES3/gl3.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>

// ==============================================================================
// HILBERT CUBE EFFECT
// Generates a recursive 3D space-filling curve (Hilbert Curve) and rendering it 
// as a glowing neon wireframe. Demonstrates deferred geometry generation on the CPU.
// ==============================================================================
class HilbertCubeEffect : public KinematicEffect {
public:
    HilbertCubeEffect();
    ~HilbertCubeEffect() override;

    // --- Core API Implementation ---
    
    // Identifies the plugin in the Lua Control Plane.
    const char* get_name() const override;
    
    // Exposes runtime-configurable parameters to Lua.
    // Note: We will merge our visual parameters with the physics parameters 
    // inherited from KinematicEffect.
    std::vector<EffectParameter> get_parameters() const override;
    
    // Updates internal state when Lua changes a parameter.
    void set_parameter(const std::string& name, const EffectParameterValue& value) override;

    // Compiles shaders and initializes the Kinematic base class.
    bool initialize(ICoreContext* core, uint32_t width, uint32_t height) override;
    
    // Calculates physics and dispatches draw calls to the GPU.
    void render(uint32_t width, uint32_t height, float dt) override;
    
    // Safely releases GPU memory to prevent leaks during hot-reloads.
    void cleanup() override;

private:
    // ==========================================================================
    // OPENGL STATE
    // ==========================================================================
    GLuint program = 0;
    
    // Buffers for the continuous Hilbert Curve (Line Strip)
    GLuint curve_vao = 0;
    GLuint curve_vbo = 0;
    GLsizei curve_vertex_count = 0;

    // Buffers for the bounding Box (Discrete Lines)
    GLuint cube_vao = 0;
    GLuint cube_vbo = 0;
    GLuint cube_ebo = 0; // Element buffer for indexed drawing

    // Uniform locations cached to avoid expensive string lookups in the render loop
    GLuint u_model = 0;
    GLuint u_view = 0;
    GLuint u_projection = 0;
    GLuint u_line_color = 0;

    // ==========================================================================
    // VISUAL PARAMETERS (Lua Configurable)
    // Note: Physics/Transformation parameters (rotation_speed, offset, etc.) 
    // are now handled implicitly by the KinematicEffect base class.
    // ==========================================================================
    
    // The recursion depth of the curve. Higher = exponentially more vertices.
    // Kept capped at 5 to prevent extreme memory and CPU spikes.
    int hilbert_order = 4;
    
    bool draw_cube_outline = true;
    glm::vec3 curve_color = {1.0f, 0.5f, 0.0f}; // Default Orange
    glm::vec3 cube_color = {0.8f, 0.8f, 0.8f};  // Default Light Gray
    
    // Deferred Geometry Flag:
    // CPU geometry generation is heavy. We only rebuild the VBOs when Lua 
    // explicitly changes the `hilbert_order`. 
    bool needs_regeneration = true;

    // ==========================================================================
    // INTERNAL GEOMETRY GENERATORS
    // ==========================================================================
    
    // Orchestrates the creation of the Curve and pushes it to the GPU.
    void generate_hilbert_curve();
    
    // Orchestrates the creation of the bounding wireframe box.
    void generate_cube_outline();
    
    // The core recursive algorithm that calculates the 3D space-filling path.
    // Divides the space into 8 sub-octants and connects their centers.
    void hilbert3D(const glm::vec3& start, 
                   const glm::vec3& a, 
                   const glm::vec3& b, 
                   const glm::vec3& c, 
                   int order, 
                   std::vector<glm::vec3>& vertices);
};

// ==============================================================================
// C-ABI EXPORT (Plugin Manager Boundary)
// Ensures the compiled .so library can be loaded dynamically by the Wayland core 
// without C++ name mangling issues.
// ==============================================================================
extern "C" {
    IWallpaperEffectABI* create_effect();
    void destroy_effect(IWallpaperEffectABI* effect);
}

#endif // HILBERT_CUBE_EFFECT_HPP