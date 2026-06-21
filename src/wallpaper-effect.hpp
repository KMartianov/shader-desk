// src/wallpaper-effect.hpp
#pragma once
#include <string>
#include <vector>
#include <variant>
#include <glm/glm.hpp>
#include <memory>
#include "audio-data.hpp" // <-- Возвращаем аудио

class WallpaperEffect;
using EffectParameterValue = std::variant<bool, int, float, glm::vec3>;
using WallpaperEffectPtr = std::unique_ptr<WallpaperEffect, void(*)(WallpaperEffect*)>;

struct EffectParameter {
    std::string name;
    std::string description;
    EffectParameterValue value;
};

class WallpaperEffect {
public:
    virtual ~WallpaperEffect() = default;

    virtual bool initialize(uint32_t width, uint32_t height) = 0;
    virtual void render(uint32_t width, uint32_t height) = 0;
    virtual void cleanup() = 0;

    // --- РАЗДЕЛЬНЫЕ ОБРАБОТЧИКИ (С ПУСТОЙ БАЗОВОЙ РЕАЛИЗАЦИЕЙ) ---
    // Плагины переопределяют только то, что им нужно.
    virtual void handle_pointer_motion(double dx, double dy, bool is_touchpad) {}
    virtual void handle_audio_data(const AudioData& data) {}

    virtual const char* get_name() const = 0;
    virtual std::vector<EffectParameter> get_parameters() const = 0;
    virtual void set_parameter(const std::string& name, const EffectParameterValue& value) = 0;
};

extern "C" {
    WallpaperEffect* create_effect();
    void destroy_effect(WallpaperEffect* effect);
}