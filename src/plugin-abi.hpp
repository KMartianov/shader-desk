// Src/plugin-abi.hpp
#pragma once

#include <cstdint>
#include <cstddef>

// Increment this version whenever the ABI structures or virtual method signatures change.
// The PluginManager will reject any .so files compiled against a different version.
#define SHADER_DESK_ABI_VERSION 2

// ==============================================================================
// 1. SAFE DATA TYPES (POD - Plain Old Data)
// These types cross the boundary between the Core and the dynamically loaded 
// Plugins (.so). They must be strictly sized, padded, and aligned to prevent 
// Compiler-specific mangling or layout discrepancies.
// ==============================================================================

enum class ParamType : uint32_t {
    TYPE_BOOL = 0,
    TYPE_INT,
    TYPE_FLOAT,
    TYPE_VEC2,
    TYPE_VEC3,
    TYPE_VEC4,
    TYPE_STRING
};

// ==============================================================================
// PARAMETER VALUE ABI (512 Bytes / 8 Cache Lines / Power of 2)
// Explicit alignas(8) guarantees that the structure aligns perfectly in memory 
// On 64-bit systems, preventing False Sharing and cache penalties.
// ==============================================================================
struct alignas(8) ParamValueABI {
    ParamType type;      // 4 bytes (offset 0)
    uint32_t _pad0;      // 4 bytes (offset 4) -> Pads the union to an 8-byte boundary

    union {
        bool b_val;
        int32_t i_val;
        float f_val;
        float vec2_val[2];
        float vec3_val[3];
        float vec4_val[4];

        
        // 504 bytes: Accommodates Linux NAME_MAX (255) with ample overhead.
        // Safely holds long relative paths, preset names, and shader identifiers.
        char s_val[504];

        // Explicit reservation to lock the union size to exactly 504 bytes.
        // Ensures that adding a new type (e.g., mat4) in the future won't break the ABI.
        uint8_t _reserved[504]; 
    };                   // (offset 8)
};

// ABI Safety Guarantees
static_assert(sizeof(ParamValueABI) == 512, "ABI Breach: ParamValueABI must be exactly 512 bytes");
static_assert(offsetof(ParamValueABI, b_val) == 8, "ABI Breach: Union alignment error");

// ==============================================================================
// PARAMETER INFO ABI (1024 Bytes / 1 Kilobyte / Power of 2)
// Transmits the parameter's metadata from the Plugin to the Lua Engine.
// ==============================================================================
struct alignas(8) ParamInfoABI {
    char name[64];               // 64 bytes  (offset 0)
    char description[448];       // 448 bytes (offset 64) -> 64 + 448 = 512 bytes
    ParamValueABI default_value; // 512 bytes (offset 512)
};

// ABI Safety Guarantees
static_assert(sizeof(ParamInfoABI) == 1024, "ABI Breach: ParamInfoABI must be exactly 1024 bytes");


// ==============================================================================
// 2. INTERFACES (COM-style ABI / Hourglass Pattern)
// ==============================================================================
// None of these classes contain fields (member variables), only pure virtual methods.
// This completely decouples the memory layout of the Core from the Plugins and 
// Protects the virtual method table (vtable) from C++ STL ABI inconsistencies.

class IBlackBoardABI {
public:
    virtual ~IBlackBoardABI() = default;
    
    // Zero-latency data bus access
    virtual float* bind_float(const char* key) = 0;
    virtual float* bind_float_array(const char* key, size_t requested_size) = 0;
    virtual void* bind_raw(const char* key, size_t requested_size_bytes) = 0;

    // String exchange
    virtual char* bind_string(const char* key) = 0;
    virtual void set_string(const char* key, const char* value) = 0;
};

enum class LogLevel : uint32_t { INFO = 0, WARNING, ERR };

// Core context provided to all plugins upon initialization
class ICoreContextABI {
public:
    virtual ~ICoreContextABI() = default;
    
    // Returns an ABI-compatible pointer to the BlackBoard
    virtual IBlackBoardABI* get_blackboard() = 0;
    
    // C-style Callback instead of std::function to cross the boundary safely. 
    // Passing void* user_data is mandatory so the plugin can pass its 'this' pointer.
    virtual void register_epoll_fd(int fd, void (*callback)(uint32_t events, void* user_data), void* user_data) = 0;
    virtual void unregister_epoll_fd(int fd) = 0;
    
    virtual void log_message(LogLevel level, const char* source, const char* message) = 0;
    
    // Gives access to the native Wayland display if a plugin requires raw protocol access
    virtual void* get_native_display() = 0;

    // Returns the absolute file path to the plugin's isolated directory bundle
    virtual const char* get_bundle_path(const char* plugin_name) = 0;
};

// Visual Plugin Interface (Shared Libraries)
class IWallpaperEffectABI {
public:
    virtual ~IWallpaperEffectABI() = default;
    
    virtual bool initialize(ICoreContextABI* core, uint32_t width, uint32_t height) = 0;
    virtual void render(uint32_t width, uint32_t height, float dt) = 0;
    virtual void resize(uint32_t width, uint32_t height) = 0;
    virtual void set_paused(bool paused) = 0;
    virtual void cleanup() = 0;
    virtual const char* get_name() const = 0;
    
    // Safe ABI replacements for std::vector<EffectParameter>
    virtual uint32_t get_parameter_count_abi() const = 0;
    virtual void get_parameter_info_abi(uint32_t index, ParamInfoABI* out_info) const = 0;
    virtual void set_parameter_abi(const char* name, const ParamValueABI* value) = 0;
    virtual bool get_parameter_abi(const char* name, ParamValueABI* out_value) const = 0;
};

// Data Provider Interface (Background Daemons / In-process Routers)
class IDataProviderABI {
public:
    virtual ~IDataProviderABI() = default;

    virtual bool initialize(ICoreContextABI* core) = 0;
    virtual void cleanup() = 0;
    virtual const char* get_name() const = 0;
    
    // Safe ABI replacements for std::vector<EffectParameter>
    virtual uint32_t get_parameter_count_abi() const = 0;
    virtual void get_parameter_info_abi(uint32_t index, ParamInfoABI* out_info) const = 0;
    virtual void set_parameter_abi(const char* name, const ParamValueABI* value) = 0;
};