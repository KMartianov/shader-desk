// Src/lua-engine.cpp
#include "lua-engine.hpp"
#include <iostream>
#include <filesystem>
#include <map>
#include "data-provider.hpp" // Now includes the SDK wrapper (IProviderABI)
#include "embedded_fs.hpp" 

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
static void* tracy_lua_alloc(void* ud, void* ptr, size_t osize, size_t nsize) {
    (void)ud; (void)osize;
    if (nsize == 0) {
        if (ptr) { TracyFree(ptr); free(ptr); }
        return nullptr;
    }
    void* new_ptr = realloc(ptr, nsize);
    if (ptr) { TracyFree(ptr); }
    if (new_ptr) { TracyAlloc(new_ptr, nsize); }
    return new_ptr;
}
#endif

namespace fs = std::filesystem;

static std::string sanitize_plugin_name(std::string name) {
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
        if (std::isspace(c) || c == '-') return '_';
        return (char)std::tolower(c);
    });
    return name;
}

std::string LuaEngine::get_config_dir() {
    if (!config_dir.empty()) return config_dir;

    const char* xdg_config = std::getenv("XDG_CONFIG_HOME");
    if (xdg_config && *xdg_config) {
        config_dir = std::string(xdg_config) + "/interactive-wallpaper";
    } else {
        config_dir = std::string(std::getenv("HOME")) + "/.config/interactive-wallpaper";
    }
    return config_dir;
}

void LuaEngine::merge_preset_into_target(sol::table target, const sol::table& preset) {
    for (auto& kv : preset) {
        sol::object key = kv.first;
        sol::object preset_val = kv.second;
        sol::object target_val = target[key];

        if (preset_val.is<sol::table>()) {
            if (target_val.valid() && target_val.is<sol::table>()) {
                // The user also has a table (e.g., vec3). Merge recursively.
                merge_preset_into_target(target_val.as<sol::table>(), preset_val.as<sol::table>());
            } else if (!target_val.valid()) {
                // The user did not set a table. Perform a deep copy to avoid reference leaks.
                sol::table new_table = lua.create_table();
                merge_preset_into_target(new_table, preset_val.as<sol::table>());
                target[key] = new_table;
            }
        } else {
            // Primitive type (number, string, bool).
            // Write from preset ONLY if the user hasn't overridden it in init.lua
            if (!target_val.valid()) {
                target[key] = preset_val;
            }
        }
    }
}



bool LuaEngine::load() {
    std::string dir = get_config_dir();
    fs::path plugins_dir = fs::path(dir) / "plugins";
    fs::path init_lua_path = fs::path(dir) / "init.lua";

    frame_callback = sol::nil; // Clear the hook from any previous script execution

    // ==============================================================================
    // 1. STATE INITIALIZATION & MEMORY MANAGEMENT
    // Completely recreate the Lua state. This prevents memory leaks during hot-reloads
    // and purges any dangling references from old scripts.
    // ==============================================================================
    #ifdef TRACY_ENABLE
    lua = sol::state(nullptr, tracy_lua_alloc, nullptr);
    #else
    lua = sol::state();
    #endif
    
    lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::os, 
                       sol::lib::string, sol::lib::table, sol::lib::package, sol::lib::io);

    // ==============================================================================
    // 2. SECURITY SANDBOXING (RCE & I/O Protection)
    // Prevent malicious themes/presets from executing shell commands or deleting 
    // user files. 'os.getenv' is retained to allow fetching system theme colors (e.g., Pywal).
    // ==============================================================================
    lua["os"]["execute"] = sol::nil;
    lua["os"]["remove"]  = sol::nil;
    lua["os"]["rename"]  = sol::nil;
    lua["os"]["exit"]    = sol::nil;
    lua["io"]["popen"]   = sol::nil;

    // Restrict io.open to Read-Only mode safely via a Lua override closure
    lua.safe_script(R"(
        local original_open = io.open
        io.open = function(filename, mode)
            local m = mode or "r"
            if m:match("w") or m:match("a") or m:match("+") then
                return nil, "Security Error: Write access denied in Shader Desk"
            end
            return original_open(filename, m)
        end
        io.output = nil
        io.write = nil
    )", sol::script_pass_on_error);
    
    // ==============================================================================
    // 3. MODULE RESOLUTION (Physical & Virtual File Systems)
    // ==============================================================================
    std::string package_path = lua["package"]["path"];
    lua["package"]["path"] = package_path + ";" + dir + "/?.lua;./src/defaults/?.lua";

    // Route internal system modules strictly to the embedded Virtual File System (VFS).
    // This guarantees they are always available, tamper-proof, and load instantly from RAM.
    lua["package"]["preload"]["ctl"] = [](sol::this_state s) -> sol::object {
        sol::state_view lua(s);
        auto content = EmbeddedFS::get_file("ctl.lua");
        sol::load_result chunk = content ? lua.load(std::string(*content))
                                        : lua.load("error('CRITICAL: ctl.lua not found in VFS')");
        if (!chunk.valid()) {
            sol::error err = chunk;
            std::cerr << "\033[31m[LuaEngine] ctl.lua compile error: " << err.what() << "\033[0m\n";
            return sol::make_object(lua, sol::lua_nil);
        }
        sol::protected_function pf = chunk;
        sol::protected_function_result result = pf();
        if (!result.valid()) {
            sol::error err = result;
            std::cerr << "\033[31m[LuaEngine] ctl.lua runtime error: " << err.what() << "\033[0m\n";
            return sol::make_object(lua, sol::lua_nil);
        }
        return result;
    };

    lua["package"]["preload"]["providers"] = [dir](sol::this_state s) -> sol::object {
        sol::state_view lua(s);

        fs::path user_path  = fs::path(dir) / "providers.lua";
        fs::path local_path  = "./src/defaults/providers.lua";

        sol::load_result chunk;
        std::string source_label;

        if (fs::exists(user_path)) {
            chunk = lua.load_file(user_path.string());
            source_label = "USER DISK: " + user_path.string();
        } else if (fs::exists(local_path)) {
            chunk = lua.load_file(local_path.string());
            source_label = "LOCAL WORKSPACE: " + local_path.string();
        } else {
            auto content = EmbeddedFS::get_file("providers.lua");
            chunk = content ? lua.load(std::string(*content))
                            : lua.load("error('CRITICAL: providers.lua not found in VFS')");
            source_label = "EMBEDDED VFS";
        }

        if (!chunk.valid()) {
            sol::error err = chunk;
            std::cerr << "\033[31m[LuaEngine] providers.lua compile error: " << err.what() << "\033[0m\n";
            return sol::make_object(lua, sol::lua_nil);
        }

        std::cout << "\033[35m[LuaEngine] providers <- " << source_label << "\033[0m\n";

        // КРИТИЧНО: load()/load_file() только компилируют chunk.
        // Чтобы тело файла реально выполнилось — chunk нужно ВЫЗВАТЬ.
        sol::protected_function pf = chunk;
        sol::protected_function_result result = pf();
        if (!result.valid()) {
            sol::error err = result;
            std::cerr << "\033[31m[LuaEngine] providers.lua runtime error: " << err.what() << "\033[0m\n";
            return sol::make_object(lua, sol::lua_nil);
        }
        return result;
    };

    // ==============================================================================
    // 4. GLOBAL ENVIRONMENT SETUP
    // Prepare the standardized table structure before evaluating any user scripts.
    // ==============================================================================
    lua["config"] = lua.create_table();
    
    sol::table core = lua.create_table();
    lua["core"] = core;
    
    sol::table utils = lua.create_table();
    sol::table debug = lua.create_table();
    core["utils"] = utils;
    core["debug"] = debug;

    // Bind the C++ Microkernel native methods to the Lua 'core' table
    if (current_core) {
        bind_core_api(current_core);
    }

    // Dynamic Preset Loader: Resolves the physical path of the requested plugin 
    // and recursively merges the preset data into the target configuration table.
    utils["apply_preset"] = [this](sol::table target, const std::string& plugin_name, const std::string& preset_name) {
        if (!current_core) return;
        std::string bundle_dir = current_core->get_bundle_path(plugin_name.c_str());
        if (bundle_dir.empty()) {
            std::cerr << "\033[33m[Warning] Bundle not found for: " << plugin_name << "\033[0m\n";
            return;
        }

        std::string preset_path = bundle_dir + "/presets/" + preset_name + ".lua";
        if (!fs::exists(preset_path)) {
            std::cout << "\033[33m[Warning] Preset not found in bundle: " << preset_path << "\033[0m\n";
            return;
        }
        
        auto result = lua.safe_script_file(preset_path, sol::script_pass_on_error);
        if (result.valid() && result.get_type() == sol::type::table) {
            sol::table preset_data = result;
            this->merge_preset_into_target(target, preset_data);
            std::cout << "  -> Applied preset '" << preset_name << "' from '" << plugin_name << "'\n";
        } else {
            sol::error err = result;
            std::cerr << "\033[31m[Error] Failed to load preset '" << preset_name << "': " << err.what() << "\033[0m\n";
        }
    };

    // ==============================================================================
    // 5. CASCADING CONFIGURATION BOOTSTRAP (Plugins)
    // Auto-load all conf.d style plugin configurations from ~/.config/.../plugins/
    // using safe_script_file to ensure a single corrupted file doesn't halt the boot.
    // ==============================================================================
    if (fs::exists(plugins_dir) && fs::is_directory(plugins_dir)) {
        for (const auto& entry : fs::directory_iterator(plugins_dir)) {
            if (entry.path().extension() == ".lua") {
                auto result = lua.safe_script_file(entry.path().string(), sol::script_pass_on_error);
                if (!result.valid()) {
                    sol::error err = result;
                    std::cerr << "\033[33m[Warning] Error loading plugin config " 
                              << entry.path().filename().string() << ": " << err.what() << "\033[0m\n";
                }
            }
        }
    }

    // ==============================================================================
    // 6. MAIN CONFIGURATION RESOLUTION (init.lua)
    // Fallback Hierarchy: 1. User Disk -> 2. Local Workspace -> 3. Embedded RAM (VFS)
    // ==============================================================================
    std::string target_file = "";
    if (fs::exists(init_lua_path)) {
        target_file = init_lua_path.string();
    } else if (fs::exists("./src/defaults/init.lua")) {
        target_file = "./src/defaults/init.lua";
    }

    auto vfs_init = EmbeddedFS::get_file("init.lua");

    if (!target_file.empty()) {
        auto result = lua.safe_script_file(target_file, sol::script_pass_on_error);
        if (!result.valid()) {
            sol::error err = result;
            std::cerr << "\033[31m[LuaEngine] Runtime error in config:\n" << err.what() << "\033[0m\n";
            std::cerr << "Falling back to embedded VFS configuration." << std::endl;
            
            if (vfs_init) {
                lua.safe_script(std::string(*vfs_init), sol::script_pass_on_error);
            }
        } else {
            std::cout << "[LuaEngine] Loaded config: " << target_file << std::endl;
        }
    } else {
        // Pure Zero-Dependency System Mode. Boot directly from the binary's .rodata segment.
        if (vfs_init) {
            lua.safe_script(std::string(*vfs_init), sol::script_pass_on_error);
            std::cout << "[LuaEngine] Loaded embedded VFS fallback config." << std::endl;
        } else {
            std::cerr << "\033[31m[LuaEngine] CRITICAL: init.lua missing from VFS!\033[0m\n";
            return false;
        }
    }

    return true;
}

// --- PROVIDER CONFIGURATION IMPLEMENTATION ---
// Now accepting safe IDataProviderABI*
bool LuaEngine::configure_provider(IDataProviderABI* provider) {
    if (!provider) return false;

    bool enabled = true;
    sol::table p_conf = sol::nil; // Will hold the provider's specific config if it exists in Lua

    // ==============================================================================
    // 1. SAFE LUA EXTRACTION
    // We navigate the Lua tables defensively. If the user removed the provider's 
    // Config entirely from init.lua, we still proceed to reset it to C++ defaults.
    // ==============================================================================
    sol::object core_obj = lua["core"];
    if (core_obj.is<sol::table>()) {
        sol::table core = core_obj.as<sol::table>();
        sol::object providers_obj = core["providers"];
        
        if (providers_obj.is<sol::table>()) {
            sol::table providers = providers_obj.as<sol::table>();
            sol::object p_conf_obj = providers[provider->get_name()];
            
            if (p_conf_obj.is<sol::table>()) {
                p_conf = p_conf_obj.as<sol::table>();
                // Check the 'enabled' flag. Default is true if missing.
                enabled = p_conf.get_or("enabled", true);
            }
        }
    }

    // If explicitly disabled by the user in Lua, abort configuration. 
    // The PluginManager will safely call cleanup() and shut it down.
    if (!enabled) return false;

    // ==============================================================================
    // 2. ABI-DRIVEN PARAMETER RESOLUTION (Fixes the "Sticky Parameter" Bug)
    // We iterate over the parameters declared by the C++ Plugin, NOT the Lua keys.
    // This guarantees that missing Lua parameters are correctly reset to defaults.
    // ==============================================================================
    uint32_t count = provider->get_parameter_count_abi();
    
    for (uint32_t i = 0; i < count; ++i) {
        ParamInfoABI info;
        provider->get_parameter_info_abi(i, &info);
        
        ParamType expected_type = info.default_value.type;
        std::string key = info.name;

        // Step A: Start with the C++ default value (Fallback state)
        ParamValueABI abi_val = info.default_value; 

        // Step B: Attempt to override with the value from Lua (if present)
        if (p_conf.valid()) {
            sol::object lua_val = p_conf[key];

            if (lua_val.valid()) {
                try {
                    if (expected_type == ParamType::TYPE_BOOL && lua_val.is<bool>()) {
                        abi_val.b_val = lua_val.as<bool>();
                    } 
                    else if (expected_type == ParamType::TYPE_INT && lua_val.is<double>()) {
                        abi_val.i_val = lua_val.as<int>();
                    } 
                    else if (expected_type == ParamType::TYPE_FLOAT && lua_val.is<double>()) {
                        abi_val.f_val = static_cast<float>(lua_val.as<double>());
                    }
                    else if (expected_type == ParamType::TYPE_VEC2 && lua_val.is<sol::table>()) {
                        sol::table t = lua_val.as<sol::table>();
                        abi_val.vec2_val[0] = t.get_or(1, 0.0f);
                        abi_val.vec2_val[1] = t.get_or(2, 0.0f);
                    }
                    else if (expected_type == ParamType::TYPE_VEC3 && lua_val.is<sol::table>()) {
                        sol::table t = lua_val.as<sol::table>();
                        abi_val.vec3_val[0] = t.get_or(1, 0.0f);
                        abi_val.vec3_val[1] = t.get_or(2, 0.0f);
                        abi_val.vec3_val[2] = t.get_or(3, 0.0f);
                    }
                    else if (expected_type == ParamType::TYPE_VEC4 && lua_val.is<sol::table>()) {
                        sol::table t = lua_val.as<sol::table>();
                        abi_val.vec4_val[0] = t.get_or(1, 0.0f);
                        abi_val.vec4_val[1] = t.get_or(2, 0.0f);
                        abi_val.vec4_val[2] = t.get_or(3, 0.0f);
                        abi_val.vec4_val[3] = t.get_or(4, 0.0f);
                    }
                    else if (expected_type == ParamType::TYPE_STRING && lua_val.is<std::string>()) {
                        std::string lua_str = lua_val.as<std::string>();
                        // Ensure strict null-termination and prevent buffer overflow in C-ABI
                        size_t max_len = sizeof(abi_val.s_val) - 1;
                        std::strncpy(abi_val.s_val, lua_str.c_str(), max_len);
                        abi_val.s_val[max_len] = '\0'; 
                    }
                } catch (const sol::error& e) {
                    std::cerr << "[LuaEngine] Type Error for provider parameter '" 
                              << key << "': " << e.what() << std::endl;
                }
            }
        }

        // Step C: Send the resolved value back to the provider.
        // If the user deleted the line from init.lua, this safely applies the clean default.
        provider->set_parameter_abi(key.c_str(), &abi_val);
    }

    return true;
}

// ==============================================================================
// SAFE PROXY OBJECT FOR FLUENT API
// This struct acts as a safe bridge between Lua and the C++ plugins.
// It never holds direct pointers to plugins, completely preventing segfaults 
// If a plugin is hot-reloaded or destroyed during the frame.
// ==============================================================================
struct LuaLayerProxy {
    std::string output_name;
    std::string tag;
    LuaEngine* engine;

    // Sets a parameter and returns a reference to itself to allow method chaining.
    LuaLayerProxy& set(const std::string& param_name, sol::object val) {
        if (!engine->get_layer_by_tag) return *this;
        
        // Resolve the pointer dynamically on every call.
        IWallpaperEffectABI* effect = engine->get_layer_by_tag(output_name, tag);
        if (!effect) return *this; // Safe fail if layer is dead

        ParamType expected_type;
        bool found = false;
        uint32_t count = effect->get_parameter_count_abi();
        
        for (uint32_t i = 0; i < count; ++i) {
            ParamInfoABI info;
            effect->get_parameter_info_abi(i, &info);
            if (param_name == info.name) { 
                expected_type = info.default_value.type;
                found = true;
                break;
            }
        }

        if (!found) return *this; 

        ParamValueABI abi_val;
        abi_val.type = expected_type;
        try {
            if (expected_type == ParamType::TYPE_BOOL && val.is<bool>()) {
                abi_val.b_val = val.as<bool>();
            } else if (expected_type == ParamType::TYPE_INT && val.is<double>()) {
                abi_val.i_val = val.as<int>();
            } else if (expected_type == ParamType::TYPE_FLOAT && val.is<double>()) {
                abi_val.f_val = static_cast<float>(val.as<double>());
            } else if (expected_type == ParamType::TYPE_VEC2 && val.is<sol::table>()) {
                sol::table t = val.as<sol::table>();
                abi_val.vec2_val[0] = t.get_or(1, 0.0f); 
                abi_val.vec2_val[1] = t.get_or(2, 0.0f); 
            } else if (expected_type == ParamType::TYPE_VEC3 && val.is<sol::table>()) {
                sol::table t = val.as<sol::table>();
                abi_val.vec3_val[0] = t.get_or(1, 0.0f); 
                abi_val.vec3_val[1] = t.get_or(2, 0.0f); 
                abi_val.vec3_val[2] = t.get_or(3, 0.0f);
            } else if (expected_type == ParamType::TYPE_VEC4 && val.is<sol::table>()) {
                sol::table t = val.as<sol::table>();
                abi_val.vec4_val[0] = t.get_or(1, 0.0f); 
                abi_val.vec4_val[1] = t.get_or(2, 0.0f); 
                abi_val.vec4_val[2] = t.get_or(3, 0.0f);
                abi_val.vec4_val[3] = t.get_or(4, 0.0f);
            } else if (expected_type == ParamType::TYPE_STRING && val.is<std::string>()) {
                size_t max_len = sizeof(abi_val.s_val) - 1;
                std::strncpy(abi_val.s_val, val.as<std::string>().c_str(), max_len);
                abi_val.s_val[max_len] = '\0';
            } else {
                return *this;
            }
            
            effect->set_parameter_abi(param_name.c_str(), &abi_val);
        } catch (...) {
            // Silently ignore type mismatches from Lua to prevent crashing the frame loop
        }

        return *this; 
    }

    // Zero-allocation method for hot-path parameter updates (e.g., inside on_frame).
    // Prevents LuaJIT from creating garbage collection spikes when updating colors/vectors.
    LuaLayerProxy& set_vec2(const std::string& param_name, float x, float y) {
        if (!engine->get_layer_by_tag) return *this;
        IWallpaperEffectABI* effect = engine->get_layer_by_tag(output_name, tag);
        if (!effect) return *this;

        ParamValueABI abi_val;
        abi_val.type = ParamType::TYPE_VEC2;
        abi_val.vec2_val[0] = x;
        abi_val.vec2_val[1] = y;

        effect->set_parameter_abi(param_name.c_str(), &abi_val);
        return *this;
    }

    LuaLayerProxy& set_vec3(const std::string& param_name, float x, float y, float z) {
        if (!engine->get_layer_by_tag) return *this;
        IWallpaperEffectABI* effect = engine->get_layer_by_tag(output_name, tag);
        if (!effect) return *this;

        ParamValueABI abi_val;
        abi_val.type = ParamType::TYPE_VEC3;
        abi_val.vec3_val[0] = x;
        abi_val.vec3_val[1] = y;
        abi_val.vec3_val[2] = z;

        effect->set_parameter_abi(param_name.c_str(), &abi_val);
        return *this;
    }

    LuaLayerProxy& set_vec4(const std::string& param_name, float x, float y, float z, float w) {
        if (!engine->get_layer_by_tag) return *this;
        IWallpaperEffectABI* effect = engine->get_layer_by_tag(output_name, tag);
        if (!effect) return *this;

        ParamValueABI abi_val;
        abi_val.type = ParamType::TYPE_VEC4;
        abi_val.vec4_val[0] = x;
        abi_val.vec4_val[1] = y;
        abi_val.vec4_val[2] = z;
        abi_val.vec4_val[3] = w;

        effect->set_parameter_abi(param_name.c_str(), &abi_val);
        return *this;
    }

    // Reads the current parameter state directly from the C++ plugin.
    sol::object get(const std::string& param_name, sol::this_state s) {
        if (!engine->get_layer_by_tag) return sol::make_object(s, sol::nil);
        
        IWallpaperEffectABI* effect = engine->get_layer_by_tag(output_name, tag);
        if (!effect) return sol::make_object(s, sol::nil);

        ParamValueABI abi_val;
        if (effect->get_parameter_abi(param_name.c_str(), &abi_val)) {
            switch (abi_val.type) {
                case ParamType::TYPE_FLOAT:  return sol::make_object(s, abi_val.f_val);
                case ParamType::TYPE_INT:    return sol::make_object(s, abi_val.i_val);
                case ParamType::TYPE_BOOL:   return sol::make_object(s, abi_val.b_val);
                case ParamType::TYPE_STRING: return sol::make_object(s, std::string(abi_val.s_val));
                case ParamType::TYPE_VEC2: {
                    sol::table t = sol::state_view(s).create_table();
                    t[1] = abi_val.vec2_val[0]; 
                    t[2] = abi_val.vec2_val[1]; 
                    return t;
                }
                case ParamType::TYPE_VEC3: {
                    sol::table t = sol::state_view(s).create_table();
                    t[1] = abi_val.vec3_val[0]; 
                    t[2] = abi_val.vec3_val[1]; 
                    t[3] = abi_val.vec3_val[2];
                    return t;
                }
                case ParamType::TYPE_VEC4: {
                    sol::table t = sol::state_view(s).create_table();
                    t[1] = abi_val.vec4_val[0]; 
                    t[2] = abi_val.vec4_val[1]; 
                    t[3] = abi_val.vec4_val[2];
                    t[4] = abi_val.vec4_val[3];
                    return t;
                }
            }
        }
        return sol::make_object(s, sol::nil);
    }
};

// Accepting ICoreContextABI*
// Core API to Lua binding
void LuaEngine::bind_core_api(ICoreContextABI* core) {
    if (!core) return;
    current_core = core; 
    
    if (lua["core"].get_type() == sol::type::none) {
        lua["core"] = lua.create_table();
    }
    sol::table core_table = lua["core"];
    
    // --- 1. WRITE TO BLACKBOARD ---
    core_table["set_string"] = [core](const std::string& key, const std::string& val) {
        core->get_blackboard()->set_string(key.c_str(), val.c_str());
    };

    core_table["set_float_array"] = [core](const std::string& key, sol::table t) {
        float* ptr = core->get_blackboard()->bind_float_array(key.c_str(), 256);
        if (ptr) {
            std::cout << "\033[36m[LuaEngine] Writing array '" << key << "' to BlackBoard. First values: [";
            
            // Lua tables are 1-indexed. We safely iterate up to 256.
            for (size_t i = 0; i < 256; ++i) {
                sol::object obj = t[i + 1];
                
                // BUGFIX: Lua numbers are strictly 'double'. We must extract them as double 
                // first, then explicitly cast to C++ 'float'. sol::optional<float> fails silently.
                if (obj.valid() && obj.is<double>()) {
                    ptr[i] = static_cast<float>(obj.as<double>());
                } else {
                    ptr[i] = 0.0f; // Default missing elements to 0.0 (Flat EQ)
                }
                
                if (i < 5) std::cout << ptr[i] << (i == 4 ? "" : ", ");
            }
            std::cout << "...]\033[0m" << std::endl;
        }
    };

    // --- GLOBAL 3D CAMERA CONTROL ---
    core_table["set_camera"] = [core](sol::table pos_tbl, sol::table tgt_tbl, sol::optional<float> fov_opt) {
        float* p_pos = core->get_blackboard()->bind_float_array("scene.camera.pos", 3);
        float* p_tgt = core->get_blackboard()->bind_float_array("scene.camera.target", 3);
        float* p_fov = core->get_blackboard()->bind_float("scene.camera.fov");
        float* p_active = core->get_blackboard()->bind_float("scene.camera.active");

        if (p_pos) {
            p_pos[0] = pos_tbl.get_or(1, 0.0f);
            p_pos[1] = pos_tbl.get_or(2, 0.0f);
            p_pos[2] = pos_tbl.get_or(3, 0.0f);
        }
        if (p_tgt) {
            p_tgt[0] = tgt_tbl.get_or(1, 0.0f);
            p_tgt[1] = tgt_tbl.get_or(2, 0.0f);
            p_tgt[2] = tgt_tbl.get_or(3, 0.0f);
        }
        if (p_fov) {
            *p_fov = fov_opt.value_or(45.0f);
        }
        if (p_active) {
            *p_active = 1.0f; // Flag indicating the global camera is overriding defaults
        }
    };

    // --- 2. READ FROM BLACKBOARD ---
    core_table["get_float"] = [core](const std::string& key, sol::optional<float> default_val) -> float {
        float* ptr = core->get_blackboard()->bind_float(key.c_str());
        return ptr ? *ptr : default_val.value_or(0.0f);
    };

    core_table["get_string"] = [core](const std::string& key, sol::optional<std::string> default_val) -> std::string {
        char* ptr = core->get_blackboard()->bind_string(key.c_str());
        return (ptr && ptr[0] != '\0') ? std::string(ptr) : default_val.value_or("");
    };

    core_table["get_float_array"] = [core, this](const std::string& key, size_t requested_size) -> sol::table {
        size_t safe_size = std::min(requested_size, size_t(256));
        sol::table result = lua.create_table(safe_size, 0); 
        
        float* ptr = core->get_blackboard()->bind_float_array(key.c_str(), safe_size);
        if (ptr) {
            for (size_t i = 0; i < safe_size; ++i) {
                result[i + 1] = ptr[i];
            }
        }
        return result;
    };

    // --- 3. REGISTER PER-FRAME HOOK ---
    core_table["on_frame"] = [this](sol::protected_function cb) {
        if (cb.valid()) {
            this->frame_callback = cb;
            std::cout << "[LuaEngine] Registered per-frame animation hook." << std::endl;
        }
    };

    // --- 4. ASYNCHRONOUS TIMERS (EPOLL TIMERFD) ---
    core_table["set_interval"] = [this](int ms, sol::protected_function callback) -> int {
        if (ms <= 0 || !callback.valid() || !current_core) return -1;

        if (active_timers.size() >= 32) {
            std::cerr << "[LuaEngine] ERROR: Maximum timer limit (32) reached. "
                    << "Are you calling set_interval inside on_frame?" << std::endl;
            return -1;
        }

        int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
        if (tfd == -1) return -1;

        struct itimerspec ts{};
        ts.it_interval.tv_sec = ms / 1000;
        ts.it_interval.tv_nsec = (ms % 1000) * 1000000;
        ts.it_value = ts.it_interval;

        if (timerfd_settime(tfd, 0, &ts, nullptr) == -1) {
            close(tfd);
            return -1;
        }

        active_timers[tfd] = [this, tfd, callback](uint32_t events) {
            if (active_timers.find(tfd) == active_timers.end()) return;

            uint64_t expirations;
            ssize_t s = read(tfd, &expirations, sizeof(expirations));
            if (s == sizeof(expirations)) {
                auto result = callback();
                if (!result.valid()) {
                    sol::error err = result;
                    std::cerr << "[Lua Timer Error] " << err.what() << std::endl;
                }
            }
        };

        current_core->register_epoll_fd(tfd, [](uint32_t events, void* user_data) {
            auto* cb = static_cast<std::function<void(uint32_t)>*>(user_data);
            if (cb && *cb) (*cb)(events);
        }, &active_timers[tfd]);

        return tfd;
    };

    core_table["clear_interval"] = [this](int tfd) {
        if (tfd < 0) return;
        if (active_timers.erase(tfd)) {
            if (current_core) current_core->unregister_epoll_fd(tfd);
            close(tfd);
        }
    };

    // --- 5. LAYER PROXY API (FLUENT DESIGN) ---
    lua.new_usertype<LuaLayerProxy>("LayerProxy",
        "set", &LuaLayerProxy::set,
        "set_vec2", &LuaLayerProxy::set_vec2,
        "set_vec3", &LuaLayerProxy::set_vec3,
        "set_vec4", &LuaLayerProxy::set_vec4,
        "get", &LuaLayerProxy::get
    );

    core_table["get_layer"] = [this](const std::string& output_name, const std::string& tag) {
        return LuaLayerProxy{output_name, tag, this};
    };

    core_table["load_scene"] = [this](const std::string& scene_name) -> sol::object {
        std::vector<std::string> search_paths = {
            get_config_dir() + "/scenes/" + scene_name + ".lua",
            
            #ifdef LOCAL_SCENES_DIR
            std::string(LOCAL_SCENES_DIR) + "/" + scene_name + ".lua",
            #endif
            #ifdef SYSTEM_SCENES_DIR
            std::string(SYSTEM_SCENES_DIR) + "/" + scene_name + ".lua",
            #endif
            
            "./src/defaults/scenes/" + scene_name + ".lua",
            "/usr/share/shader-desk/scenes/" + scene_name + ".lua"
        };

        // 1. Try to load from physical disk (prioritizes user modifications)
        for (const auto& path : search_paths) {
            if (std::filesystem::exists(path)) {
                auto result = lua.load_file(path);
                if (result.valid()) {
                    sol::protected_function pf = result;
                    return pf().get<sol::object>();
                }
            }
        }

        // 2. Fallback to Virtual File System (RAM)
        std::string vfs_key = "scenes/" + scene_name + ".lua";
        auto vfs_content = EmbeddedFS::get_file(vfs_key);
        
        if (vfs_content) {
            auto result = lua.load(std::string(*vfs_content));
            if (result.valid()) {
                sol::protected_function pf = result;
                auto exec_res = pf();
                if (exec_res.valid()) return exec_res.get<sol::object>();
            }
        }

        std::cerr << "\033[33m[Lua] Scene '" << scene_name << "' not found on disk or in VFS.\033[0m\n";
        return sol::nil;
    };
}

// Per-frame hook implementation with error spam protection
void LuaEngine::on_frame(float dt, const std::string& output_name) {
    if (!frame_callback.valid()) return;

    // Call the Lua function: on_frame(dt, "DP-1")
    auto result = frame_callback(dt, output_name);
    
    if (!result.valid()) {
        sol::error err = result;
        std::cerr << "\033[31m[Lua Frame Error on " << output_name << "] " << err.what() << "\033[0m" << std::endl;
        std::cerr << "[LuaEngine] Faulty on_frame callback disabled to prevent 144Hz log spam." << std::endl;
        // Reset the callback on the very first error so the compositor continues working
        frame_callback = sol::nil; 
    }
}

OutputConfig LuaEngine::get_output_config(const std::string& output_name, const std::string& output_desc) {
    OutputConfig res;
    sol::table core = lua["core"];
    if (!core.valid()) return res;

    std::string fallback_effect = core.get_or("default_effect", core.get_or("active_effect", std::string("")));
    res.fps_limit = core.get_or("fps_limit", 0.0f);

    sol::object outputs_obj = core["outputs"];
    if (!outputs_obj.is<sol::table>()) {
        if (!fallback_effect.empty()) res.layers.push_back({fallback_effect, fallback_effect, sol::nil, false});
        return res;

    }

    sol::table outputs = outputs_obj.as<sol::table>();
    sol::object out_conf_obj = outputs[output_name];
    if (!out_conf_obj.valid() && !output_desc.empty()) out_conf_obj = outputs[output_desc];
    if (!out_conf_obj.valid()) out_conf_obj = outputs["*"];


    if (out_conf_obj.is<sol::table>()) {
        sol::table out_conf = out_conf_obj.as<sol::table>();
        res.fps_limit = out_conf.get_or("fps_limit", res.fps_limit);
        res.fbo_scale = out_conf.get_or("fbo_scale", 1.0f); // Read FBO scale

        if (out_conf["layers"].is<sol::table>()) {
            sol::table layers_table = out_conf["layers"];
            for (auto& kv : layers_table) {
                if (!kv.second.is<sol::table>()) continue;
                sol::table layer_tbl = kv.second.as<sol::table>();
                
                std::string eff_name = layer_tbl.get_or("effect", std::string(""));
                if (eff_name.empty()) continue;

                std::string tag = layer_tbl.get_or("tag", eff_name); 
                sol::table settings = layer_tbl["settings"].is<sol::table>() ? layer_tbl["settings"] : lua.create_table();
                bool is_post = layer_tbl.get_or("postprocess", false);
                bool clear_depth = layer_tbl.get_or("clear_depth", true);

                // --- Base Layer Preset ---
                std::string preset = layer_tbl.get_or("preset", std::string(""));
                if (!preset.empty()) {
                    std::string applied = settings.get_or("_preset_applied", std::string(""));
                    if (applied != preset) {
                        sol::function apply_preset = core["utils"]["apply_preset"];
                        if (apply_preset.valid()) {
                            apply_preset(settings, eff_name, preset);
                            settings["_preset_applied"] = preset; 
                        }
                    }
                }

                // Parse Nested Filters ---
                std::vector<LayerConfig> parsed_filters;
                if (layer_tbl["filters"].is<sol::table>()) {
                    sol::table filters_tbl = layer_tbl["filters"];
                    for (auto& f_kv : filters_tbl) {
                        if (!f_kv.second.is<sol::table>()) continue;
                        sol::table f_tbl = f_kv.second.as<sol::table>();
                        
                        std::string f_eff_name = f_tbl.get_or("effect", std::string(""));
                        if (f_eff_name.empty()) continue;
                        
                        std::string f_tag = f_tbl.get_or("tag", f_eff_name);
                        sol::table f_settings = f_tbl["settings"].is<sol::table>() ? f_tbl["settings"] : lua.create_table();
                        
                        // Filter Preset
                        std::string f_preset = f_tbl.get_or("preset", std::string(""));
                        if (!f_preset.empty()) {
                            std::string f_applied = f_settings.get_or("_preset_applied", std::string(""));
                            if (f_applied != f_preset) {
                                sol::function apply_preset = core["utils"]["apply_preset"];
                                if (apply_preset.valid()) {
                                    apply_preset(f_settings, f_eff_name, f_preset);
                                    f_settings["_preset_applied"] = f_preset;
                                }
                            }
                        }
                        
                        // Filters are inherently post-processing and don't manage scene depth
                        parsed_filters.push_back({f_eff_name, f_tag, f_settings, true, false, {}});
                    }
                }

                // Push the parsed configuration including the nested filters
                res.layers.push_back({eff_name, tag, settings, is_post, clear_depth, parsed_filters});
            }
        } else {
            // Old format (1 effect)
            std::string eff_name = out_conf.get_or("effect", fallback_effect);
            if (!eff_name.empty()) {
                sol::table settings = out_conf["settings"].is<sol::table>() ? out_conf["settings"] : lua.create_table();
                res.layers.push_back({eff_name, eff_name, settings, false, true});
            }

        }
    } else if (!fallback_effect.empty()) {
        res.layers.push_back({fallback_effect, fallback_effect, sol::nil, false});

    }
    return res;
}

void LuaEngine::clear_timers() {
    // Active_timers is now a map, iterate by key-value pairs
    for (auto& pair : active_timers) {
        int tfd = pair.first;
        if (current_core) {
            current_core->unregister_epoll_fd(tfd);
        }
        close(tfd);
    }
    active_timers.clear();
}

bool LuaEngine::reload() {
    std::string dir = get_config_dir();
    std::filesystem::path user_init = std::filesystem::path(dir) / "init.lua";
    std::filesystem::path local_init = "./src/defaults/init.lua";
    
    std::filesystem::path active_init;

    // Determine which config is currently the main one (Priority 1 vs Priority 2)
    if (std::filesystem::exists(user_init)) {
        active_init = user_init;
    } else if (std::filesystem::exists(local_init)) {
        active_init = local_init;
    }

    // --- PRE-VALIDATION (Syntax) ---
    if (!active_init.empty()) {
        sol::load_result syntax_check = lua.load_file(active_init.string());
        if (!syntax_check.valid()) {
            sol::error err = syntax_check;
            std::cerr << "\033[31m[Lua Syntax Error in " << active_init.filename().string() 
                      << "] Aborting reload:\n" << err.what() << "\033[0m" << std::endl;
            return false; // Abort! Keep the old State working.
        }
    }

    // If syntax is valid, safely recreate the environment
    std::cout << "[LuaEngine] Syntax OK. Clearing active timers before reload..." << std::endl;
    clear_timers(); 
    return load();
}

std::string LuaEngine::get_active_effect() const {
    sol::table core = lua["core"];
    if (core.valid()) {
        return core.get_or("active_effect", std::string(""));
    }
    return "";
}

bool LuaEngine::is_interactive() const {
    sol::table core = lua["core"];
    if (core.valid()) {
        return core.get_or("interactive", true);
    }
    return true;
}

// Now accepting safe IWallpaperEffectABI*
void LuaEngine::apply_effect_settings(IWallpaperEffectABI* effect, 
                                      const std::string& effect_name, 
                                      const sol::table& output_specific_settings) 
{
    if (!effect) return;

    sol::table base_settings = lua["config"][effect_name];
    uint32_t count = effect->get_parameter_count_abi();
    
    for (uint32_t i = 0; i < count; ++i) {
        ParamInfoABI info;
        effect->get_parameter_info_abi(i, &info);
        ParamType expected_type = info.default_value.type;
        std::string key = info.name;

        // --- CASCADING VALUE SELECTION ---
        sol::object final_lua_val = sol::nil;
        
        // 1. Take from the global plugin config (plugins/*.lua)
        if (base_settings.valid()) {
            final_lua_val = base_settings[key];
        }
        // 2. Override with specific monitor settings (init.lua), if any
        if (output_specific_settings.valid()) {
            sol::object local_val = output_specific_settings[key];
            if (local_val.valid()) {
                final_lua_val = local_val;
            }
        }

        // 3. Base value from C++ ABI (if nothing is set in Lua)
        ParamValueABI abi_val = info.default_value;

        // --- PARSE LUA -> C-ABI (If the value is overridden) ---
        if (final_lua_val.valid()) {
            try {
                if (expected_type == ParamType::TYPE_BOOL && final_lua_val.is<bool>()) {
                    abi_val.b_val = final_lua_val.as<bool>();
                } 
                else if (expected_type == ParamType::TYPE_INT && final_lua_val.is<double>()) {
                    abi_val.i_val = final_lua_val.as<int>();
                } 
                else if (expected_type == ParamType::TYPE_FLOAT && final_lua_val.is<double>()) {
                    abi_val.f_val = static_cast<float>(final_lua_val.as<double>());
                } 
                else if (expected_type == ParamType::TYPE_VEC2 && final_lua_val.is<sol::table>()) {
                    sol::table t = final_lua_val.as<sol::table>();
                    abi_val.vec2_val[0] = t.get_or(1, 0.0f);
                    abi_val.vec2_val[1] = t.get_or(2, 0.0f);
                }
                else if (expected_type == ParamType::TYPE_VEC3 && final_lua_val.is<sol::table>()) {
                    sol::table t = final_lua_val.as<sol::table>();
                    abi_val.vec3_val[0] = t.get_or(1, 0.0f);
                    abi_val.vec3_val[1] = t.get_or(2, 0.0f);
                    abi_val.vec3_val[2] = t.get_or(3, 0.0f);
                }
                else if (expected_type == ParamType::TYPE_VEC4 && final_lua_val.is<sol::table>()) {
                    sol::table t = final_lua_val.as<sol::table>();
                    abi_val.vec4_val[0] = t.get_or(1, 0.0f);
                    abi_val.vec4_val[1] = t.get_or(2, 0.0f);
                    abi_val.vec4_val[2] = t.get_or(3, 0.0f);
                    abi_val.vec4_val[3] = t.get_or(4, 0.0f);
                }
                else if (expected_type == ParamType::TYPE_STRING && final_lua_val.is<std::string>()) {
                    std::string lua_str = final_lua_val.as<std::string>();
                    size_t max_len = sizeof(abi_val.s_val) - 1;
                    std::strncpy(abi_val.s_val, lua_str.c_str(), max_len);
                    abi_val.s_val[max_len] = '\0';
                }
            } catch (const sol::error& e) {
                std::cerr << "[LuaEngine] Type Error for parameter '" << key << "': " << e.what() << std::endl;
            }
        }
        
        // 4. Apply to the plugin!
        effect->set_parameter_abi(key.c_str(), &abi_val);
    }
}