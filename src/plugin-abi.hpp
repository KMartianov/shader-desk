// src/plugin-abi.hpp
#pragma once

#include <cstdint>
#include <cstddef>

// ==============================================================================
// 1. БЕЗОПАСНЫЕ ТИПЫ ДАННЫХ (POD)
// ==============================================================================

enum class ParamType : uint32_t {
    TYPE_BOOL = 0,
    TYPE_INT,
    TYPE_FLOAT,
    TYPE_VEC3,
    TYPE_STRING
};

struct ParamValueABI {
    ParamType type;
    union {
        bool b_val;
        int i_val;
        float f_val;
        float vec3_val[3];
        const char* s_val; // Гарантированно нуль-терминированная строка

        // РЕЗЕРВ: Жесткая фиксация размера union (64 байта).
        // Гарантирует, что при добавлении матриц (mat4) старые плагины не сломаются.
        uint8_t _padding[64]; 
    };
};

struct ParamInfoABI {
    const char* name;
    const char* description;
    ParamValueABI default_value;
};

// ==============================================================================
// 2. ИНТЕРФЕЙСЫ (COM-style ABI)
// ==============================================================================
// Ни один класс не содержит полей (переменных), только чистые виртуальные методы.
// Это защищает таблицу виртуальных методов (vtable) от искажений компилятором.

class IBlackBoardABI {
public:
    virtual ~IBlackBoardABI() = default;
    
    virtual float* bind_float(const char* key) = 0;
    virtual float* bind_float_array(const char* key, size_t requested_size) = 0;
    virtual char* bind_string(const char* key) = 0;
    virtual void set_string(const char* key, const char* value) = 0;
};

class ICoreContextABI {
public:
    virtual ~ICoreContextABI() = default;
    
    // Возвращаем ABI-совместимый указатель на BlackBoard
    virtual IBlackBoardABI* get_blackboard() = 0;
    
    // C-style Callback вместо std::function! 
    // Передача void* user_data обязательна, чтобы плагин мог прокинуть указатель на this (себя).
    virtual void register_epoll_fd(int fd, void (*callback)(uint32_t events, void* user_data), void* user_data) = 0;
    virtual void unregister_epoll_fd(int fd) = 0;
};

class IWallpaperEffectABI {
public:
    virtual ~IWallpaperEffectABI() = default;

    virtual bool initialize(ICoreContextABI* core, uint32_t width, uint32_t height) = 0;
    virtual void render(uint32_t width, uint32_t height) = 0;
    virtual void cleanup() = 0;
    
    virtual const char* get_name() const = 0;
    
    // ABI замена для std::vector<EffectParameter>
    virtual uint32_t get_parameter_count() const = 0;
    virtual void get_parameter_info(uint32_t index, ParamInfoABI* out_info) const = 0;
    virtual void set_parameter(const char* name, const ParamValueABI* value) = 0;
};

class IDataProviderABI {
public:
    virtual ~IDataProviderABI() = default;

    virtual bool initialize(ICoreContextABI* core) = 0;
    virtual void cleanup() = 0;
    
    virtual const char* get_name() const = 0;
    
    // ABI замена для std::vector<EffectParameter>
    virtual uint32_t get_parameter_count() const = 0;
    virtual void get_parameter_info(uint32_t index, ParamInfoABI* out_info) const = 0;
    virtual void set_parameter(const char* name, const ParamValueABI* value) = 0;
};