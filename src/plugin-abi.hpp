// src/plugin-abi.hpp
#pragma once

#include <cstdint>
#include <cstddef>

// ==============================================================================
// 1. SAFE DATA TYPES (POD)
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
        const char* s_val; // Guaranteed null-terminated string

        // RESERVED: Fixed union size (64 bytes).
        // Ensures forward ABI compatibility (e.g., if mat4 is added later, old plugins won't break).
        uint8_t _padding[64]; 
    };
};

struct ParamInfoABI {
    const char* name;
    const char* description;
    ParamValueABI default_value;
};

// ==============================================================================
// 2. INTERFACES (COM-style ABI)
// ==============================================================================
// None of these classes contain fields (member variables), only pure virtual methods.
// This protects the virtual method table (vtable) from compiler-specific mangling.

class IBlackBoardABI {
public:
    virtual ~IBlackBoardABI() = default;
    
    virtual float* bind_float(const char* key) = 0;
    virtual float* bind_float_array(const char* key, size_t requested_size) = 0;
    virtual void* bind_raw(const char* key, size_t requested_size_bytes) = 0;

    virtual char* bind_string(const char* key) = 0;
    virtual void set_string(const char* key, const char* value) = 0;
};

enum class LogLevel : uint32_t { INFO = 0, WARNING, ERR };


class ICoreContextABI {
public:
    virtual ~ICoreContextABI() = default;
    
    // Returns an ABI-compatible pointer to the BlackBoard
    virtual IBlackBoardABI* get_blackboard() = 0;
    
    // C-style Callback instead of std::function. 
    // Passing void* user_data is mandatory so the plugin can pass its 'this' pointer.
    virtual void register_epoll_fd(int fd, void (*callback)(uint32_t events, void* user_data), void* user_data) = 0;
    virtual void unregister_epoll_fd(int fd) = 0;
    virtual void log_message(LogLevel level, const char* source, const char* message) = 0;
    virtual void* get_native_display() = 0;

    virtual const char* get_bundle_path(const char* plugin_name) = 0;
};

class IWallpaperEffectABI {
public:
    virtual ~IWallpaperEffectABI() = default;

    virtual bool initialize(ICoreContextABI* core, uint32_t width, uint32_t height) = 0;
    virtual void render(uint32_t width, uint32_t height, float dt) = 0;

    virtual void resize(uint32_t width, uint32_t height) = 0;
    virtual void set_paused(bool paused) = 0;


    virtual void cleanup() = 0;
    
    virtual const char* get_name() const = 0;
    
    // ABI replacement for std::vector<EffectParameter>
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
    
    // ABI replacement for std::vector<EffectParameter>
    virtual uint32_t get_parameter_count() const = 0;
    virtual void get_parameter_info(uint32_t index, ParamInfoABI* out_info) const = 0;
    virtual void set_parameter(const char* name, const ParamValueABI* value) = 0;
};