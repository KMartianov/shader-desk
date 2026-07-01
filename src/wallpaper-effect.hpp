// src/wallpaper-effect.hpp
#pragma once

#include "plugin-abi.hpp" 
#include <vector>
#include <string>
#include <variant>
#include <memory>
#include <glm/glm.hpp>

// Алиас для обратной совместимости. 
// Плагины смогут использовать ICoreContext, как и раньше.
using ICoreContext = ICoreContextABI;

// Типы данных, которые можно настраивать через Lua
using EffectParameterValue = std::variant<bool, int, float, glm::vec3, std::string>;

// Умный указатель для загрузчика (Ядра)
using WallpaperEffectPtr = std::unique_ptr<IWallpaperEffectABI, void(*)(IWallpaperEffectABI*)>;

// Структура, описывающая один настраиваемый параметр плагина (C++ стиль)
struct EffectParameter {
    std::string name;
    std::string description;
    EffectParameterValue value;
};

// ==============================================================================
// ИНТЕРФЕЙС ВИЗУАЛЬНОГО ПЛАГИНА (HEADER-ONLY SDK)
// ==============================================================================
class WallpaperEffect : public IWallpaperEffectABI {
public:
    virtual ~WallpaperEffect() = default;

    // --- 1. ПРИВЫЧНЫЙ C++ API ДЛЯ АВТОРОВ ПЛАГИНОВ ---
    // Авторы плагинов переопределяют именно эти методы.
    virtual bool initialize(ICoreContext* core, uint32_t width, uint32_t height) = 0;
    virtual void render(uint32_t width, uint32_t height) = 0;
    virtual void cleanup() = 0;
    virtual const char* get_name() const = 0;

    virtual std::vector<EffectParameter> get_parameters() const = 0;
    virtual void set_parameter(const std::string& name, const EffectParameterValue& value) = 0;

    // ==============================================================================
    // --- 2. СКРЫТЫЙ СЛОЙ ABI (МАГИЯ ПЕСОЧНЫХ ЧАСОВ) ---
    // Эти методы помечены как 'final', чтобы плагины не могли их сломать.
    // ==============================================================================
    
    uint32_t get_parameter_count() const final {
        if (!cache_valid) {
            param_cache = get_parameters(); // Вызываем C++ метод плагина 1 раз
            cache_valid = true;
        }
        return static_cast<uint32_t>(param_cache.size());
    }

    void get_parameter_info(uint32_t index, ParamInfoABI* out_info) const final {
        if (index >= param_cache.size()) return;
        const auto& p = param_cache[index];
        
        // Отдаем указатели на строки из кэша (это безопасно, так как вектор живет в классе)
        out_info->name = p.name.c_str();
        out_info->description = p.description.c_str();
        
        // Упаковка std::variant в C-Union
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
        // Распаковка C-Union в std::variant
        switch (value->type) {
            case ParamType::TYPE_BOOL: cpp_val = value->b_val; break;
            case ParamType::TYPE_INT: cpp_val = value->i_val; break;
            case ParamType::TYPE_FLOAT: cpp_val = value->f_val; break;
            case ParamType::TYPE_VEC3: cpp_val = glm::vec3(value->vec3_val[0], value->vec3_val[1], value->vec3_val[2]); break;
            case ParamType::TYPE_STRING: cpp_val = std::string(value->s_val); break;
        }
        // Передаем распакованные данные в красивый C++ метод плагина
        this->set_parameter(std::string(name), cpp_val);
    }

private:
    mutable std::vector<EffectParameter> param_cache;
    mutable bool cache_valid = false;
};

// ==============================================================================
// ФУНКЦИИ ЭКСПОРТА ABI
// ==============================================================================
extern "C" {
    IWallpaperEffectABI* create_effect();
    void destroy_effect(IWallpaperEffectABI* effect);
}