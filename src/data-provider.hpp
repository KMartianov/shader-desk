#pragma once

#include "plugin-abi.hpp"
#include "wallpaper-effect.hpp" // Берем EffectParameter и EffectParameterValue оттуда
#include <cstring>

// ==============================================================================
// DATA PROVIDER INTERFACE (SMART PROVIDER SDK)
// ==============================================================================
class IDataProvider : public IDataProviderABI {
public:
    virtual ~IDataProvider() = default;
    
    // --- 1. STANDARD C++ API ---
    virtual const char* get_name() const = 0;
    virtual bool initialize(ICoreContext* core) = 0;
    virtual void cleanup() = 0;

    virtual std::vector<EffectParameter> get_parameters() const = 0;
    virtual void set_parameter(const std::string& name, const EffectParameterValue& value) = 0;

    // --- 2. HIDDEN ABI LAYER ---
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
        
        // БЕЗОПАСНОЕ КОПИРОВАНИЕ СТРОК В ABI
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
        } else if (std::holds_alternative<glm::vec3>(p.value)) {
            out_info->default_value.type = ParamType::TYPE_VEC3;
            auto v = std::get<glm::vec3>(p.value);
            out_info->default_value.vec3_val[0] = v.x;
            out_info->default_value.vec3_val[1] = v.y;
            out_info->default_value.vec3_val[2] = v.z;
        } else if (std::holds_alternative<std::string>(p.value)) {
            out_info->default_value.type = ParamType::TYPE_STRING;
            const std::string& str = std::get<std::string>(p.value);
            std::strncpy(out_info->default_value.s_val, str.c_str(), 255);
            out_info->default_value.s_val[255] = '\0';
        }
    }

    void set_parameter_abi(const char* name, const ParamValueABI* value) final {
        EffectParameterValue cpp_val;
        switch (value->type) {
            case ParamType::TYPE_BOOL: cpp_val = value->b_val; break;
            case ParamType::TYPE_INT: cpp_val = value->i_val; break;
            case ParamType::TYPE_FLOAT: cpp_val = value->f_val; break;
            case ParamType::TYPE_VEC3: cpp_val = glm::vec3(value->vec3_val[0], value->vec3_val[1], value->vec3_val[2]); break;
            case ParamType::TYPE_STRING: cpp_val = std::string(value->s_val); break;
        }
        this->set_parameter(std::string(name), cpp_val);
    }

private:
    mutable std::vector<EffectParameter> param_cache;
    mutable bool cache_valid = false;
};

// Signatures for C-ABI export from .so libraries
extern "C" {
    typedef IDataProviderABI* (*CreateProviderFunc)();
    typedef void (*DestroyProviderFunc)(IDataProviderABI*);
}