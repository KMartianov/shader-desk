#pragma once

#include "wallpaper-effect.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// GLM requires this macro to enable the quaternion extension.
// Defining it here ensures that any plugin inheriting KinematicEffect
// will compile seamlessly without throwing GLM_GTX errors.
#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/gtx/quaternion.hpp>

#include <algorithm>

// ==============================================================================
// KINEMATIC EFFECT BASE CLASS
// Inherit from this class instead of WallpaperEffect to automatically gain 
// 3D physics, inertia, center of mass (pivot), and Global Camera integration.
// ==============================================================================
class KinematicEffect : public WallpaperEffect {
protected:
    ICoreContext* m_core = nullptr;

    // --- Kinematic Properties (Configurable via Lua) ---
    glm::vec3 position_offset = {0.0f, 0.0f, 0.0f}; // Position in World Space
    glm::vec3 pivot_offset = {0.0f, 0.0f, 0.0f};    // Center of mass (Local Space)
    glm::vec3 rotation_axis = {0.0f, 1.0f, 0.0f};   // Current rotational force axis
    float rotation_speed = 0.0f;                    // Continuous force or Impulse
    float rotation_decay = 0.95f;                   // Friction/Inertia (0.0 to 1.0)

    // --- Internal Physics State ---
    glm::quat orientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    glm::vec3 angular_velocity = glm::vec3(0.0f);

    // --- BlackBoard Camera Pointers ---
    float* p_cam_active = nullptr;
    float* p_cam_pos = nullptr;
    float* p_cam_tgt = nullptr;
    float* p_cam_up  = nullptr; // NEW: Free 6DOF Up vector
    float* p_cam_fov = nullptr;

    // --- Computed MVP Matrices (Ready to send to OpenGL) ---
    glm::mat4 model_matrix;
    glm::mat4 view_matrix;
    glm::mat4 proj_matrix;
    glm::vec3 current_view_pos;

    // ==========================================================================
    // INITIALIZATION
    // Call this inside your plugin's initialize() method.
    // ==========================================================================
    void init_kinematics(ICoreContext* core) {
        m_core = core;
        p_cam_active = core->get_blackboard()->bind_float("scene.camera.active");
        p_cam_pos    = core->get_blackboard()->bind_float_array("scene.camera.pos", 3);
        p_cam_tgt    = core->get_blackboard()->bind_float_array("scene.camera.target", 3);
        p_cam_up     = core->get_blackboard()->bind_float_array("scene.camera.up", 3);
        p_cam_fov    = core->get_blackboard()->bind_float("scene.camera.fov");
    }

    // ==========================================================================
    // PHYSICS TICK & MATRIX COMPUTATION
    // Call this at the start of your plugin's render() method.
    // ==========================================================================
    void update_kinematics(float dt, float aspect_ratio) {
        // 1. Apply Impulse or Continuous Force
        if (std::abs(rotation_speed) > 0.0001f && glm::length(rotation_axis) > 0.0001f) {
            angular_velocity += glm::normalize(rotation_axis) * rotation_speed * dt;
        }

        // 2. Apply Friction (Decay)
        angular_velocity *= rotation_decay;

        // 3. Integrate Velocity into Orientation Quaternion
        float speed = glm::length(angular_velocity);
        if (speed > 1e-5f) {
            glm::vec3 axis = angular_velocity / speed; // Safe normalize
            glm::quat rotation_delta = glm::angleAxis(speed * dt, axis);
            orientation = glm::normalize(rotation_delta * orientation);
        }

        // 4. Compute Model Matrix (World Space + Pivot Center of Mass)
        model_matrix = glm::mat4(1.0f);
        model_matrix = glm::translate(model_matrix, position_offset);
        model_matrix = glm::translate(model_matrix, pivot_offset);
        model_matrix = model_matrix * glm::toMat4(orientation);
        model_matrix = glm::translate(model_matrix, -pivot_offset);

        // 5. Compute 6DOF Camera (View & Projection Matrices)
        if (p_cam_active && *p_cam_active > 0.5f && p_cam_pos && p_cam_tgt && p_cam_up && p_cam_fov) {
            current_view_pos = glm::vec3(p_cam_pos[0], p_cam_pos[1], p_cam_pos[2]);
            glm::vec3 tgt(p_cam_tgt[0], p_cam_tgt[1], p_cam_tgt[2]);
            glm::vec3 up(p_cam_up[0], p_cam_up[1], p_cam_up[2]);
            
            // Defend against degenerate up vector causing NaN matrices during math edge-cases
            if (glm::length(up) < 0.0001f) {
                up = glm::vec3(0.0f, 1.0f, 0.0f);
            } else {
                up = glm::normalize(up);
            }
            
            view_matrix = glm::lookAt(current_view_pos, tgt, up);
            proj_matrix = glm::perspective(glm::radians(*p_cam_fov), aspect_ratio, 0.1f, 100.0f);
        } else {
            current_view_pos = glm::vec3(0.0f, 0.0f, 2.5f);
            view_matrix = glm::lookAt(current_view_pos, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            proj_matrix = glm::perspective(glm::radians(45.0f), aspect_ratio, 0.1f, 100.0f);
        }
    }

    // ==========================================================================
    // PARAMETER REGISTRATION API
    // Use these helpers in your plugin's get/set parameter overrides.
    // ==========================================================================
    std::vector<EffectParameter> get_kinematic_params() const {
        return {
            {"offset", "World Space Position (X, Y, Z)", position_offset},
            {"rotation_axis", "Local rotation axis (X, Y, Z)", rotation_axis},
            {"rotation_speed", "Continuous rotation force / impulse", rotation_speed},
            {"rotation_decay", "Inertial friction (0.0-1.0)", rotation_decay},
            {"pivot_offset", "Center of mass adjustment", pivot_offset}
        };
    }

    bool set_kinematic_param(const std::string& name, const EffectParameterValue& value) {
        try {
            if (name == "offset") { position_offset = std::get<glm::vec3>(value); return true; }
            if (name == "rotation_axis") { rotation_axis = std::get<glm::vec3>(value); return true; }
            if (name == "rotation_speed") { rotation_speed = std::get<float>(value); return true; }
            if (name == "rotation_decay") { rotation_decay = std::clamp(std::get<float>(value), 0.0f, 1.0f); return true; }
            if (name == "pivot_offset") { pivot_offset = std::get<glm::vec3>(value); return true; }
        } catch (const std::bad_variant_access&) {}
        return false;
    }
};