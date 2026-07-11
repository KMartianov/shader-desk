// src/lua-engine.cpp
#include "lua-engine.hpp"
#include <iostream>
#include <filesystem>
#include <map>
#include "data-provider.hpp" // Now includes the SDK wrapper (IProviderABI)
#include "embedded_init.hpp" 
#include "embedded_ctl.hpp" 

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
                // У юзера тоже таблица (например, vec3). Сливаем рекурсивно.
                merge_preset_into_target(target_val.as<sol::table>(), preset_val.as<sol::table>());
            } else if (!target_val.valid()) {
                // Юзер не задал таблицу. Делаем глубокую копию, чтобы избежать утечки ссылок.
                sol::table new_table = lua.create_table();
                merge_preset_into_target(new_table, preset_val.as<sol::table>());
                target[key] = new_table;
            }
        } else {
            // Примитивный тип (число, строка, bool).
            // Записываем из пресета ТОЛЬКО если юзер не переопределил его в init.lua
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

    frame_callback = sol::nil; // Clear the hook from the previous script

    // 1. Completely clear the state (useful for hot-reload to prevent memory leaks)
    #ifdef TRACY_ENABLE
    lua = sol::state(nullptr, tracy_lua_alloc, nullptr);
    #else
    lua = sol::state();
    #endif
    
    // 2. Load standard safe Lua libraries
    lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::os, sol::lib::string, sol::lib::table, sol::lib::package);

    // ==============================================================================
    // SECURITY PATCH: Sandbox the 'os' library.
    // We allow 'os.getenv' for themes (like Pywal), but strictly disable RCE 
    // (Remote Code Execution) and file deletion to protect the user's system.
    // ==============================================================================
    lua["os"]["execute"] = sol::nil;
    lua["os"]["remove"]  = sol::nil;
    lua["os"]["rename"]  = sol::nil;
    lua["os"]["exit"]    = sol::nil;
    
    // ==============================================================================
    // SMART MODULE RESOLUTION (package.path & package.preload)
    // ==============================================================================
    // Lua will search for modules (like ctl.lua) in the user config dir first, 
    // then fallback to the local source tree (useful for development).
    std::string package_path = lua["package"]["path"];
    lua["package"]["path"] = package_path + ";" + dir + "/?.lua;./src/defaults/?.lua";

    // If 'ctl.lua' cannot be found on the filesystem at all (e.g., system-wide install 
    // with no user config generated yet), load it directly from the embedded C++ string!
    lua["package"]["preload"]["ctl"] = [](sol::this_state s) {
        sol::state_view lua(s);
        return lua.load(Embedded::CTL_LUA);
    };

    // 3. Create global tables to prevent script crashes on missing globals
    lua["config"] = lua.create_table();
    
    sol::table core = lua.create_table();
    lua["core"] = core;
    
    sol::table utils = lua.create_table();
    sol::table debug = lua.create_table();
    core["utils"] = utils;
    core["debug"] = debug;

    // Rebind the C++ API to the newly created sol::state!
    if (current_core) {
        bind_core_api(current_core);
    }

    // --- PRESET SYSTEM REGISTRATION ---
    utils["apply_preset"] = [this](sol::table target, const std::string& plugin_name, const std::string& preset_name) {
        if (!current_core) return;

        // Request bundle path from the Core via C-ABI
        std::string bundle_dir = current_core->get_bundle_path(plugin_name.c_str());
        if (bundle_dir.empty()) {
            std::cerr << "\033[33m[Warning] Bundle not found for: " << plugin_name << "\033[0m\n";
            return;
        }

        // Preset path: .../effects/<bundle>/presets/<preset>.lua
        std::string preset_path = bundle_dir + "/presets/" + preset_name + ".lua";
        
        if (!fs::exists(preset_path)) {
            std::cout << "\033[33m[Warning] Preset not found in bundle: " << preset_path << "\033[0m\n";
            return;
        }
        
        // Safely load the preset without crashing the engine on syntax errors inside the preset
        auto result = lua.safe_script_file(preset_path, sol::script_pass_on_error);
        if (result.valid() && result.get_type() == sol::type::table) {
            sol::table preset_data = result;
            this->merge_preset_into_target(target, preset_data);
            std::cout << "  -> Applied preset '" << preset_name << "' from bundle of '" << plugin_name << "'\n";
        } else {
            sol::error err = result;
            std::cerr << "\033[31m[Error] Failed to load preset '" << preset_name << "': " << err.what() << "\033[0m\n";
        }
    };

    // ==============================================================================
    // 4. CASCADING CONFIGURATION BOOTSTRAP
    // First, execute all auto-generated plugin configs (conf.d style from plugins/*.lua).
    // Using safe_script_file to ensure a single corrupted plugin file doesn't halt the boot.
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
    // 5. INTELLIGENT INIT.LUA LOADING (User -> Local Dev -> Embedded Fallback)
    // ==============================================================================
    std::string target_file = "";
    if (fs::exists(init_lua_path)) {
        target_file = init_lua_path.string();
    } else if (fs::exists("./src/defaults/init.lua")) {
        target_file = "./src/defaults/init.lua";
    }

    if (!target_file.empty()) {
        // Safe execution: If the user made a fatal logic error in init.lua, 
        // we catch it gracefully and fallback to the embedded C++ string.
        auto result = lua.safe_script_file(target_file, sol::script_pass_on_error);
        
        if (!result.valid()) {
            sol::error err = result;
            std::cerr << "\033[31m[LuaEngine] Runtime error in config:\n" << err.what() << "\033[0m\n";
            std::cerr << "Falling back to embedded safe configuration." << std::endl;
            lua.safe_script(Embedded::INIT_LUA, sol::script_pass_on_error);
        } else {
            std::cout << "[LuaEngine] Loaded config: " << target_file << std::endl;
        }
    } else {
        // Pure portable/system mode. No files needed.
        lua.safe_script(Embedded::INIT_LUA, sol::script_pass_on_error);
        std::cout << "[LuaEngine] Loaded embedded fallback config." << std::endl;
    }

    return true;
}


// --- PROVIDER CONFIGURATION IMPLEMENTATION ---
// [ABI UPDATE]: Now accepting safe IDataProviderABI*
bool LuaEngine::configure_provider(IDataProviderABI* provider) {
    if (!provider) return false;

    bool enabled = true;
    sol::table p_conf = sol::nil; // Will hold the provider's specific config if it exists in Lua

    // ==============================================================================
    // 1. SAFE LUA EXTRACTION
    // We navigate the Lua tables defensively. If the user removed the provider's 
    // config entirely from init.lua, we still proceed to reset it to C++ defaults.
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
                    else if (expected_type == ParamType::TYPE_VEC3 && lua_val.is<sol::table>()) {
                        sol::table t = lua_val.as<sol::table>();
                        // Safely extract table values, defaulting to 0.0f if the table is too short
                        abi_val.vec3_val[0] = t.get_or(1, 0.0f);
                        abi_val.vec3_val[1] = t.get_or(2, 0.0f);
                        abi_val.vec3_val[2] = t.get_or(3, 0.0f);
                    }
                    else if (expected_type == ParamType::TYPE_STRING && lua_val.is<std::string>()) {
                        std::string lua_str = lua_val.as<std::string>();
                        // Ensure strict null-termination and prevent buffer overflow in C-ABI
                        std::strncpy(abi_val.s_val, lua_str.c_str(), 255);
                        abi_val.s_val[255] = '\0'; 
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
// if a plugin is hot-reloaded or destroyed during the frame.
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
            } else if (expected_type == ParamType::TYPE_VEC3 && val.is<sol::table>()) {
                sol::table t = val.as<sol::table>();
                abi_val.vec3_val[0] = t.get_or(1, 0.0f); 
                abi_val.vec3_val[1] = t.get_or(2, 0.0f); 
                abi_val.vec3_val[2] = t.get_or(3, 0.0f);
            } else if (expected_type == ParamType::TYPE_STRING && val.is<std::string>()) {
                std::strncpy(abi_val.s_val, val.as<std::string>().c_str(), 255);
                abi_val.s_val[255] = '\0';
            } else {
                return *this;
            }
            
            effect->set_parameter_abi(param_name.c_str(), &abi_val);
        } catch (...) {
            // Silently ignore type mismatches from Lua to prevent crashing the frame loop
        }

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
                case ParamType::TYPE_VEC3: {
                    sol::table t = sol::state_view(s).create_table();
                    t[1] = abi_val.vec3_val[0]; 
                    t[2] = abi_val.vec3_val[1]; 
                    t[3] = abi_val.vec3_val[2];
                    return t;
                }
            }
        }
        return sol::make_object(s, sol::nil);
    }
};

// --- DEBUG API IMPLEMENTATION ---
// [ABI UPDATE]: Accepting ICoreContextABI*
// [UPDATED] Привязка API ядра к Lua (Исправлена ошибка компиляции Sol2)
// [UPDATED] Привязка API ядра к Lua
void LuaEngine::bind_core_api(ICoreContextABI* core) {
    if (!core) return;
    current_core = core; 
    
    if (lua["core"].get_type() == sol::type::none) {
        lua["core"] = lua.create_table();
    }
    sol::table core_table = lua["core"];
    
    // --- 1. ЗАПИСЬ В BLACKBOARD ---
    core_table["set_string"] = [core](const std::string& key, const std::string& val) {
        core->get_blackboard()->set_string(key.c_str(), val.c_str());
    };

    core_table["set_float_array"] = [core](const std::string& key, sol::table t) {
        size_t size = std::min(t.size(), size_t(256));
        float* ptr = core->get_blackboard()->bind_float_array(key.c_str(), size);
        if (ptr) {
            for (size_t i = 0; i < size; ++i) {
                ptr[i] = t.get_or(i + 1, 0.0f);
            }
        }
    };

    // --- 2. ЧТЕНИЕ ИЗ BLACKBOARD ---
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

    // --- 3. РЕГИСТРАЦИЯ ПОКАДРОВОГО ХУКА ---
    core_table["on_frame"] = [this](sol::protected_function cb) {
        if (cb.valid()) {
            this->frame_callback = cb;
            std::cout << "[LuaEngine] Registered per-frame animation hook." << std::endl;
        }
    };

    // --- 4. АСИНХРОННЫЕ ТАЙМЕРЫ (EPOLL TIMERFD) ---
    core_table["set_interval"] = [this](int ms, sol::protected_function callback) -> int {
        if (ms <= 0 || !callback.valid() || !current_core) return -1;

        // ЗАЩИТА: Лимит в 32 активных таймера на весь Lua-скрипт
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
    // Register the proxy object structure in Sol2
    lua.new_usertype<LuaLayerProxy>("LayerProxy",
        "set", &LuaLayerProxy::set,
        "get", &LuaLayerProxy::get
    );

    // Factory function to generate and return proxy objects to Lua.
    // Example usage in Lua: local bg = core.get_layer("DP-1", "bg_back")
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

        for (const auto& path : search_paths) {
            if (std::filesystem::exists(path)) {
                auto result = lua.load_file(path);
                if (result.valid()) {
                    sol::protected_function pf = result;
                    auto exec_res = pf();
                    if (exec_res.valid()) {
                        return exec_res.get<sol::object>();
                    } else {
                        sol::error err = exec_res;
                        std::cerr << "\033[31m[Lua] Runtime error in scene '" << scene_name << "':\n" << err.what() << "\033[0m\n";
                        return sol::nil;
                    }
                } else {
                    sol::error err = result;
                    std::cerr << "\033[31m[Lua] Syntax error in scene '" << scene_name << "':\n" << err.what() << "\033[0m\n";
                    return sol::nil;
                }
            }
        }
        std::cerr << "\033[33m[Lua] Scene '" << scene_name << "' not found.\033[0m\n";
        return sol::nil;
    };


}

// [NEW] Реализация покадрового хука с защитой от спама ошибками
void LuaEngine::on_frame(float dt, const std::string& output_name) {
    if (!frame_callback.valid()) return;

    // Вызываем Lua функцию: on_frame(dt, "DP-1")
    auto result = frame_callback(dt, output_name);
    
    if (!result.valid()) {
        sol::error err = result;
        std::cerr << "\033[31m[Lua Frame Error on " << output_name << "] " << err.what() << "\033[0m" << std::endl;
        std::cerr << "[LuaEngine] Faulty on_frame callback disabled to prevent 144Hz log spam." << std::endl;
        // Сбрасываем коллбэк при первой же ошибке, чтобы композитор продолжал работать
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
        res.fbo_scale = out_conf.get_or("fbo_scale", 1.0f); // Читаем масштаб FBO

        if (out_conf["layers"].is<sol::table>()) {
            sol::table layers_table = out_conf["layers"];
            for (auto& kv : layers_table) {
                if (!kv.second.is<sol::table>()) continue;
                sol::table layer_tbl = kv.second.as<sol::table>();
                
                std::string eff_name = layer_tbl.get_or("effect", std::string(""));
                if (eff_name.empty()) continue;

                // Extract the semantic tag. If the user didn't specify one, 
                // default to the effect name itself for backward compatibility.
                std::string tag = layer_tbl.get_or("tag", eff_name); 
                
                sol::table settings = layer_tbl["settings"].is<sol::table>() ? layer_tbl["settings"] : lua.create_table();
                bool is_post = layer_tbl.get_or("postprocess", false);

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

                // Push the parsed configuration including the tag
                res.layers.push_back({eff_name, tag, settings, is_post});
            }
        } else {
            // Старый формат (1 эффект)
            std::string eff_name = out_conf.get_or("effect", fallback_effect);
            if (!eff_name.empty()) {
                sol::table settings = out_conf["settings"].is<sol::table>() ? out_conf["settings"] : lua.create_table();
                res.layers.push_back({eff_name, eff_name, settings, false});

            }
        }
    } else if (!fallback_effect.empty()) {
        res.layers.push_back({fallback_effect, fallback_effect, sol::nil, false});

    }
    return res;
}

void LuaEngine::clear_timers() {
    // [ABI UPDATE]: active_timers is now a map, iterate by key-value pairs
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

    // Определяем, какой конфиг сейчас является главным (Приоритет 1 vs Приоритет 2)
    if (std::filesystem::exists(user_init)) {
        active_init = user_init;
    } else if (std::filesystem::exists(local_init)) {
        active_init = local_init;
    }

    // --- ПРЕД-ВАЛИДАЦИЯ (Синтаксис) ---
    if (!active_init.empty()) {
        sol::load_result syntax_check = lua.load_file(active_init.string());
        if (!syntax_check.valid()) {
            sol::error err = syntax_check;
            std::cerr << "\033[31m[Lua Syntax Error in " << active_init.filename().string() 
                      << "] Aborting reload:\n" << err.what() << "\033[0m" << std::endl;
            return false; // Отмена! Оставляем старый State рабочим.
        }
    }

    // Если синтаксис верный, безопасно пересоздаем среду
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

// [ABI UPDATE]: Now accepting safe IWallpaperEffectABI*
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

        // --- КАСКАДНЫЙ ВЫБОР ЗНАЧЕНИЯ ---
        sol::object final_lua_val = sol::nil;
        
        // 1. Берем из глобального конфига плагина (plugins/*.lua)
        if (base_settings.valid()) {
            final_lua_val = base_settings[key];
        }
        // 2. Переопределяем настройками конкретного монитора (init.lua), если они есть
        if (output_specific_settings.valid()) {
            sol::object local_val = output_specific_settings[key];
            if (local_val.valid()) {
                final_lua_val = local_val;
            }
        }

        // 3. Базовое значение из C++ ABI (если в Lua вообще ничего нет)
        ParamValueABI abi_val = info.default_value;

        // --- ПАРСИНГ LUA -> C-ABI (Если значение переопределено) ---
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
                else if (expected_type == ParamType::TYPE_VEC3 && final_lua_val.is<sol::table>()) {
                    sol::table t = final_lua_val.as<sol::table>();
                    abi_val.vec3_val[0] = t.get_or(1, 0.0f);
                    abi_val.vec3_val[1] = t.get_or(2, 0.0f);
                    abi_val.vec3_val[2] = t.get_or(3, 0.0f);
                }
                else if (expected_type == ParamType::TYPE_STRING && final_lua_val.is<std::string>()) {
                    std::string lua_str = final_lua_val.as<std::string>();
                    std::strncpy(abi_val.s_val, lua_str.c_str(), 255);
                    abi_val.s_val[255] = '\0';
                }
            } catch (const sol::error& e) {
                std::cerr << "[LuaEngine] Type Error for parameter '" << key << "': " << e.what() << std::endl;
            }
        }
        
        // 4. Применяем к плагину!
        effect->set_parameter_abi(key.c_str(), &abi_val);
    }
}
