// src/wallpaper-effect.hpp

#pragma once

#include <string>
#include <vector>
#include <variant>
#include <glm/glm.hpp>
#include <memory>
#include "audio-data.hpp" // <--- ДОБАВЛЕНО

// Forward-declare the class so the unique_ptr knows about it
class WallpaperEffect;

using EffectParameterValue = std::variant<bool, int, float, glm::vec3>;

// Define the smart pointer type alias here
using WallpaperEffectPtr = std::unique_ptr<WallpaperEffect, void(*)(WallpaperEffect*)>;

struct EffectParameter {
    std::string name;
    std::string description;
    EffectParameterValue value;
};

class WallpaperEffect {
public:
    virtual ~WallpaperEffect() = default;

    // --- Core Lifecycle Methods ---
    virtual bool initialize(uint32_t width, uint32_t height) = 0;
    virtual void render(uint32_t width, uint32_t height) = 0;
    virtual void cleanup() = 0;

    // --- Input Handling ---
    virtual void handle_pointer_motion(double dx, double dy, bool is_touchpad) = 0;

    // ++ ДОБАВЛЕНО: Метод для приёма аудиоданных ++
    // Основное приложение будет вызывать этот метод для активного эффекта.
    // Реализация по умолчанию пустая, чтобы плагины без аудио-визуализации не ломались.
    virtual void handle_audio_data(const AudioData& data) {}

    // --- Metadata and Configuration Interface ---
    virtual const char* get_name() const = 0;
    virtual std::vector<EffectParameter> get_parameters() const = 0;
    virtual void set_parameter(const std::string& name, const EffectParameterValue& value) = 0;
};

// C-style functions that each plugin .so file MUST export
extern "C" {
    WallpaperEffect* create_effect();
    void destroy_effect(WallpaperEffect* effect);
}