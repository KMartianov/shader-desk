// Src/plugins/text-3d-effect/text-3d-effect.cpp
#define GLM_ENABLE_EXPERIMENTAL
#define STB_TRUETYPE_IMPLEMENTATION

#include "stb_truetype.h"
#include "text-3d-effect.hpp"
#include "shader-utils.hpp"

#include <iostream>
#include <fstream>
#include <cmath>
#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

Text3DEffect::Text3DEffect() {}

Text3DEffect::~Text3DEffect() {
    shutdown_requested.store(true);
    if (worker_thread.joinable()) {
        worker_thread.join();
    }
    on_cleanup();
}

std::vector<EffectParameter> Text3DEffect::get_parameters() const {
    return {
        {"text", "Text to display", text_string},
        {"font_path", "Absolute path to the TTF font", font_path},
        {"extrusion_depth", "3D Extrusion depth", extrusion_depth},
        {"bend_radius", "Cylinder bend radius (0 = flat)", bend_radius},
        {"text_color", "Primary text color", text_color},
        {"bg_color", "Background color", bg_color},
        // New parameters
        {"base_rotation_axis", "Ideal rotation axis (X,Y,Z)", base_rotation_axis},
        {"base_rotation_speed", "Speed around ideal axis", base_rotation_speed},
        {"mouse_sensitivity", "Mouse interaction strength", mouse_sensitivity},
        {"return_friction", "Spring-back speed to ideal state", return_friction},
        {"pivot_offset", "Center of mass adjustment", pivot_offset}
    };
}

void Text3DEffect::set_parameter(const std::string& name, const EffectParameterValue& value) {
    try {
        if (name == "text") text_string = std::get<std::string>(value);
        else if (name == "font_path") {
            auto new_font = std::get<std::string>(value);
            if (new_font != font_path) { font_path = new_font; load_font(); }
        } 
        else if (name == "extrusion_depth") extrusion_depth = std::get<float>(value);
        else if (name == "bend_radius") bend_radius = std::get<float>(value);
        else if (name == "text_color") text_color = std::get<glm::vec3>(value);
        else if (name == "bg_color") bg_color = std::get<glm::vec3>(value);
        
        else if (name == "base_rotation_axis") base_rotation_axis = std::get<glm::vec3>(value);
        else if (name == "base_rotation_speed") base_rotation_speed = std::get<float>(value);
        else if (name == "mouse_sensitivity") mouse_sensitivity = std::get<float>(value);
        else if (name == "return_friction") return_friction = std::get<float>(value);
        else if (name == "pivot_offset") pivot_offset = std::get<glm::vec3>(value);
    } catch (const std::bad_variant_access&) {
        std::cerr << "[Text3DEffect] Parameter type mismatch for: " << name << "\n";
    }
}

void Text3DEffect::load_font() {
    if (is_generating.load()) return; 

    std::ifstream file(font_path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "[Text3DEffect] Failed to open font: " << font_path << "\n";
        font_loaded = false;
        return;
    }
    
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    ttf_buffer.resize(size);
    file.read((char*)ttf_buffer.data(), size);
    font_loaded = true;
}

void Text3DEffect::start_async_sdf_generation(const std::string& target_text) {
    if (!font_loaded || ttf_buffer.empty() || target_text.empty()) return;

    if (worker_thread.joinable()) {
        worker_thread.join();
    }

    is_generating.store(true);

    worker_thread = std::thread([this, target_text]() {
        stbtt_fontinfo font;
        if (!stbtt_InitFont(&font, ttf_buffer.data(), stbtt_GetFontOffsetForIndex(ttf_buffer.data(), 0))) {
            is_generating.store(false);
            return;
        }

        const float pixel_height = 128.0f; 
        float scale = stbtt_ScaleForPixelHeight(&font, pixel_height);
        const int padding = 32; 
        const int onedge_value = 128; 
        const float pixel_dist_scale = 127.0f / (float)padding; 

        int ascent, descent, lineGap;
        stbtt_GetFontVMetrics(&font, &ascent, &descent, &lineGap);
        
        int total_width = padding * 2;
        int baseline = (int)(ascent * scale) + padding;
        int max_height = (int)((ascent - descent) * scale) + padding * 2;

        for (size_t i = 0; i < target_text.size(); ++i) {
            int advance, lsb;
            stbtt_GetCodepointHMetrics(&font, target_text[i], &advance, &lsb);
            total_width += (int)(advance * scale);
            if (i < target_text.size() - 1) {
                total_width += (int)(stbtt_GetCodepointKernAdvance(&font, target_text[i], target_text[i+1]) * scale);
            }
        }

        int max_tex_size = 2048; 
        if (total_width > max_tex_size) total_width = max_tex_size;

        float calculated_aspect = (float)total_width / (float)max_height;
        float calculated_sdf_mult = (float)padding * (2.0f / (float)max_height);

        std::vector<unsigned char> bitmap(total_width * max_height, 0);
        int current_x = padding;

        for (size_t i = 0; i < target_text.size(); ++i) {
            if (shutdown_requested.load()) {
                is_generating.store(false);
                return; 
            }

            int w, h, xoff, yoff;
            unsigned char* sdf = stbtt_GetCodepointSDF(
                &font, scale, target_text[i], 
                padding, onedge_value, pixel_dist_scale, 
                &w, &h, &xoff, &yoff
            );
            
            if (sdf) {
                for (int y = 0; y < h; ++y) {
                    for (int x = 0; x < w; ++x) {
                        int dest_x = current_x + xoff + x;
                        int dest_y = baseline + yoff + y;
                        if (dest_x >= 0 && dest_x < total_width && dest_y >= 0 && dest_y < max_height) {
                            int idx = dest_y * total_width + dest_x;
                            bitmap[idx] = std::max(bitmap[idx], sdf[y * w + x]);
                        }
                    }
                }
                stbtt_FreeSDF(sdf, nullptr);
            }
            
            int advance, lsb;
            stbtt_GetCodepointHMetrics(&font, target_text[i], &advance, &lsb);
            current_x += (int)(advance * scale);
            if (i < target_text.size() - 1) {
                current_x += (int)(stbtt_GetCodepointKernAdvance(&font, target_text[i], target_text[i+1]) * scale);
            }
            if (current_x >= max_tex_size) break;
        }

        if (!shutdown_requested.load()) {
            std::lock_guard<std::mutex> lock(async_mutex);
            next_bitmap = std::move(bitmap);
            next_width = total_width;
            next_height = max_height;
            next_aspect = calculated_aspect;
            next_sdf_mult = calculated_sdf_mult;
            texture_ready.store(true);
        }

        is_generating.store(false);
    });
}

void Text3DEffect::upload_texture_if_ready() {
    if (!texture_ready.load()) return;

    std::lock_guard<std::mutex> lock(async_mutex);
    
    if (!sdf_texture) glGenTextures(1, &sdf_texture);
    glBindTexture(GL_TEXTURE_2D, sdf_texture);
    
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, next_width, next_height, 0, GL_RED, GL_UNSIGNED_BYTE, next_bitmap.data());
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4); 
    
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    text_aspect_ratio = next_aspect;
    sdf_multiplier = next_sdf_mult;
    
    next_bitmap.clear();
    next_bitmap.shrink_to_fit();
    
    texture_ready.store(false);
}

bool Text3DEffect::initialize(ICoreContext* core, uint32_t width, uint32_t height) {
    p_accum_x = core->get_blackboard()->bind_float("mouse.accum_x");
    p_accum_y = core->get_blackboard()->bind_float("mouse.accum_y");
    p_dynamic_text = core->get_blackboard()->bind_string("lua.custom_text");

    std::string vert_src = shader_utils::load_shader_source(core, get_name(), "fullscreen_vert.glsl");
    std::string frag_src = shader_utils::load_shader_source(core, get_name(), "raymarch_frag.glsl");
    
    program = shader_utils::create_shader_program(vert_src, frag_src);
    if (!program) return false;

    u_resolution = glGetUniformLocation(program, "u_resolution");
    u_time = glGetUniformLocation(program, "u_time");
    u_inv_model = glGetUniformLocation(program, "u_inv_model"); 
    u_extrusion = glGetUniformLocation(program, "u_extrusion");
    u_bend_radius = glGetUniformLocation(program, "u_bend_radius");
    u_text_color = glGetUniformLocation(program, "u_text_color");
    u_bg_color = glGetUniformLocation(program, "u_bg_color");
    u_text_aspect = glGetUniformLocation(program, "u_text_aspect");
    u_sdf_multiplier = glGetUniformLocation(program, "u_sdf_multiplier");

    glGenVertexArrays(1, &vao);
    load_font(); 
    return true;
}

void Text3DEffect::update_rotation(float dt) {
    // 1. Update the base "ideal" rotation axis
    if (glm::length(base_rotation_axis) > 0.001f) {
        base_angle += base_rotation_speed * dt;
    }
    glm::quat base_quat = glm::angleAxis(base_angle, glm::normalize(base_rotation_axis));

    // 2. Process user influence (offset)
    if (p_accum_x && p_accum_y) {
        float dx = *p_accum_x - last_mouse_x;
        float dy = *p_accum_y - last_mouse_y;
        last_mouse_x = *p_accum_x;
        last_mouse_y = *p_accum_y;

        if (std::abs(dx) > 0.0001f || std::abs(dy) > 0.0001f) {
            // Mouse rotation axis (Invert X and Y for natural rotation)
            glm::vec3 axis = glm::vec3(dy, dx, 0.0f); 
            float angle = glm::length(axis) * mouse_sensitivity;
            axis = glm::normalize(axis);
            
            glm::quat delta = glm::angleAxis(angle, axis);
            // Multiply to accumulate the user's offset
            mouse_quat = glm::normalize(delta * mouse_quat); 
        }
    }

    // 3. Spring effect: Slowly return the mouse offset back to zero (Identity)
    if (return_friction > 0.001f) {
        mouse_quat = glm::slerp(mouse_quat, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), return_friction * dt);
    }

    // 4. Final orientation (User offset + Base rotation)
    final_orientation = base_quat * mouse_quat;
}

void Text3DEffect::render(uint32_t width, uint32_t height, float dt) {
    upload_texture_if_ready();

    std::string target_text = (p_dynamic_text && p_dynamic_text[0] != '\0') 
                                ? std::string(p_dynamic_text) 
                                : text_string;
    if (target_text.empty()) target_text = " "; 

    if (active_text != target_text && !is_generating.load()) {
        active_text = target_text; 
        start_async_sdf_generation(active_text);
    }

    update_rotation(dt); 
    time += dt;
    

    glViewport(0, 0, width, height);
    glUseProgram(program);
    
    // ====================================================================
    // CENTER OF MASS LOGIC (PIVOT)
    // Build matrix: Translate to pivot -> Rotate -> Translate back
    // ====================================================================
    glm::mat4 model_matrix = glm::mat4(1.0f);
    model_matrix = glm::translate(model_matrix, pivot_offset);
    model_matrix = model_matrix * glm::toMat4(final_orientation);
    model_matrix = glm::translate(model_matrix, -pivot_offset);
    
    glm::mat4 inv_model_matrix = glm::inverse(model_matrix);
    glUniformMatrix4fv(u_inv_model, 1, GL_FALSE, &inv_model_matrix[0][0]);
    
    glUniform2f(u_resolution, (float)width, (float)height);
    glUniform1f(u_time, time);
    glUniform1f(u_extrusion, extrusion_depth);
    glUniform1f(u_bend_radius, bend_radius);
    glUniform1f(u_text_aspect, text_aspect_ratio);
    glUniform1f(u_sdf_multiplier, sdf_multiplier);
    glUniform3fv(u_text_color, 1, &text_color[0]);
    glUniform3fv(u_bg_color, 1, &bg_color[0]);

    if (sdf_texture) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, sdf_texture);
        glUniform1i(glGetUniformLocation(program, "u_sdf_tex"), 0);
    }

    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

void Text3DEffect::on_cleanup() {
    if (program) glDeleteProgram(program);
    if (vao) glDeleteVertexArrays(1, &vao);
    if (sdf_texture) glDeleteTextures(1, &sdf_texture);
    program = vao = sdf_texture = 0;
}


// --- Exported C-functions ---
extern "C" {

    uint32_t get_abi_version() {
        return SHADER_DESK_ABI_VERSION;
    }
    
    IWallpaperEffectABI* create_effect() {
        return new Text3DEffect(); // (e.g., new HilbertCubeEffect())
    }
    void destroy_effect(IWallpaperEffectABI* effect) {
        // Static_cast safely returns us to the class to call the destructor
        delete static_cast<WallpaperEffect*>(effect);
    }
}