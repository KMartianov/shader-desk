// src/wallpaper-effect.hpp
#pragma once

#include "plugin-abi.hpp" 
#include <vector>
#include <string>
#include <variant>
#include <memory>
#include <glm/glm.hpp>

// Alias for backward compatibility. 
// Plugins can use ICoreContext as before.
using ICoreContext = ICoreContextABI;

// Data types configurable via Lua
using EffectParameterValue = std::variant<bool, int, float, glm::vec3, std::string>;

// Smart pointer for the loader (Core)
using WallpaperEffectPtr = std::unique_ptr<IWallpaperEffectABI, void(*)(IWallpaperEffectABI*)>;

// Structure describing a single configurable plugin parameter (C++ style)
struct EffectParameter {
    std::string name;
    std::string description;
    EffectParameterValue value;
};

// ==============================================================================
// VISUAL PLUGIN INTERFACE (HEADER-ONLY SDK)
// ==============================================================================
class WallpaperEffect : public IWallpaperEffectABI {
public:
    virtual ~WallpaperEffect() = default;

    // --- 1. STANDARD C++ API FOR PLUGIN AUTHORS ---
    // Plugin authors override these specific methods.
    virtual bool initialize(ICoreContext* core, uint32_t width, uint32_t height) = 0;
    virtual void render(uint32_t width, uint32_t height) = 0;
    virtual void cleanup() = 0;
    virtual const char* get_name() const = 0;

    virtual std::vector<EffectParameter> get_parameters() const = 0;
    virtual void set_parameter(const std::string& name, const EffectParameterValue& value) = 0;

    // ==============================================================================
    // --- 2. HIDDEN ABI LAYER (HOURGLASS PATTERN) ---
    // These methods are marked 'final' so plugins cannot break them.
    // ==============================================================================
    
    uint32_t get_parameter_count() const final {
        if (!cache_valid) {
            param_cache = get_parameters(); // Call the C++ plugin method exactly once
            cache_valid = true;
        }
        return static_cast<uint32_t>(param_cache.size());
    }

    void get_parameter_info(uint32_t index, ParamInfoABI* out_info) const final {
        if (index >= param_cache.size()) return;
        const auto& p = param_cache[index];
        
        // Return pointers to strings from cache (safe, as vector lives in class)
        out_info->name = p.name.c_str();
        out_info->description = p.description.c_str();
        
        // Pack std::variant into C-Union
        if (std::holds_alternative<bool>(p.value)) {
            out_info->default_value.type = ParamType::TYPE_BOOL;
            out_info->default_value.b_val = std::get<bool>(p.value);
        } else if (std::holds_alternative<int>(p.value)) {
            out_info->default_value.type = ParamType::TYPE_INT;
            out_info->default_value.i_val = std::get<int>(p.value);
        } else if (std::holds_alternative<float>(p.value)) {
            out_info->default_value.type = ParamType::TYPE_FLOAT;
            out_info->default_value.f_val = std::get<float>(p.value);
        } else if (std::holds_alternative<glm::vec3>(p.value)) {
            out_info->default_value.type = ParamType::TYPE_VEC3;
            auto v = std::get<glm::vec3>(p.value);
            out_info->default_value.vec3_val[0] = v.x;
            out_info->default_value.vec3_val[1] = v.y;
            out_info->default_value.vec3_val[2] = v.z;
        } else if (std::holds_alternative<std::string>(p.value)) {
            out_info->default_value.type = ParamType::TYPE_STRING;
            out_info->default_value.s_val = std::get<std::string>(p.value).c_str();
        }
    }

    void set_parameter(const char* name, const ParamValueABI* value) final {
        EffectParameterValue cpp_val;
        // Unpack C-Union to std::variant
        switch (value->type) {
            case ParamType::TYPE_BOOL: cpp_val = value->b_val; break;
            case ParamType::TYPE_INT: cpp_val = value->i_val; break;
            case ParamType::TYPE_FLOAT: cpp_val = value->f_val; break;
            case ParamType::TYPE_VEC3: cpp_val = glm::vec3(value->vec3_val[0], value->vec3_val[1], value->vec3_val[2]); break;
            case ParamType::TYPE_STRING: cpp_val = std::string(value->s_val); break;
        }
        // Pass unpacked data to the clean C++ plugin method
        this->set_parameter(std::string(name), cpp_val);
    }

private:
    mutable std::vector<EffectParameter> param_cache;
    mutable bool cache_valid = false;
};

// ==============================================================================
// ABI EXPORT FUNCTIONS
// ==============================================================================
extern "C" {
    IWallpaperEffectABI* create_effect();
    void destroy_effect(IWallpaperEffectABI* effect);
}