// src/lua-engine.cpp
#include "lua-engine.hpp"
#include <iostream>
#include <filesystem>
#include <map>
#include "data-provider.hpp" // Now includes the SDK wrapper (IProviderABI)

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

    frame_callback = sol::nil; // [NEW] Очищаем хук прошлого скрипта

    // 1. Completely clear the state (useful for hot-reload)
    #ifdef TRACY_ENABLE
    lua = sol::state(nullptr, tracy_lua_alloc, nullptr);
    #else
    lua = sol::state();
    #endif
    
    // 2. Load standard safe Lua libraries
    lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::os, sol::lib::string, sol::lib::table, sol::lib::package);

    // Add config directory to package.path
    std::string package_path = lua["package"]["path"];
    lua["package"]["path"] = package_path + ";" + dir + "/?.lua";

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

        // Запрашиваем путь к бандлу у Ядра через C-ABI
        std::string bundle_dir = current_core->get_bundle_path(plugin_name.c_str());
        if (bundle_dir.empty()) {
            std::cerr << "\033[33m[Warning] Bundle not found for: " << plugin_name << "\033[0m\n";
            return;
        }

        // Путь к пресету: .../effects/<bundle>/presets/<preset>.lua
        std::string preset_path = bundle_dir + "/presets/" + preset_name + ".lua";
        
        if (!fs::exists(preset_path)) {
            std::cout << "\033[33m[Warning] Preset not found in bundle: " << preset_path << "\033[0m\n";
            return;
        }
        
        try {
            sol::protected_function_result result = lua.script_file(preset_path);
            if (result.valid() && result.get_type() == sol::type::table) {
                sol::table preset_data = result;
                this->merge_preset_into_target(target, preset_data);
                std::cout << "  -> Applied preset '" << preset_name << "' from bundle of '" << plugin_name << "'\n";
            }
        } catch (const std::exception& e) {
            std::cerr << "[Error] Failed to load preset: " << e.what() << "\n";
        }
    };

    try {
        // 4. First, execute all auto-generated plugin configs (conf.d style)
        if (fs::exists(plugins_dir) && fs::is_directory(plugins_dir)) {
            for (const auto& entry : fs::directory_iterator(plugins_dir)) {
                if (entry.path().extension() == ".lua") {
                    lua.script_file(entry.path().string());
                }
            }
        }

        // 5. Then execute the main init.lua (User logic overrides)
        if (fs::exists(init_lua_path)) {
            lua.script_file(init_lua_path.string());
        } else {
            std::cerr << "Warning: init.lua not found. Run with --init-config to generate." << std::endl;
            return false;
        }

        std::cout << "Lua configuration loaded successfully." << std::endl;
        return true;

    } catch (const sol::error& e) {
        std::cerr << "CRITICAL LUA ERROR: " << e.what() << std::endl;
        std::cerr << "Falling back to previous state or defaults." << std::endl;
        return false;
    }
}

// --- PROVIDER CONFIGURATION IMPLEMENTATION ---
// [ABI UPDATE]: Now accepting safe IDataProviderABI*
bool LuaEngine::configure_provider(IDataProviderABI* provider) {
    if (!provider) return false;

    // SAFE read: ensure the object exists and is a table
    sol::object core_obj = lua["core"];
    if (!core_obj.is<sol::table>()) return true; // If not, provider is enabled by default
    sol::table core = core_obj.as<sol::table>();

    sol::object providers_obj = core["providers"];
    if (!providers_obj.is<sol::table>()) return true; 
    sol::table providers = providers_obj.as<sol::table>();

    sol::object p_conf_obj = providers[provider->get_name()];
    if (!p_conf_obj.is<sol::table>()) return true;
    sol::table p_conf = p_conf_obj.as<sol::table>();

    // Check the 'enabled' flag
    bool enabled = p_conf.get_or("enabled", true);
    if (!enabled) return false;

    // [ABI UPDATE]: Extract expected provider parameter types via C-API
    std::map<std::string, ParamType> expected_types;
    uint32_t count = provider->get_parameter_count_abi();
    for (uint32_t i = 0; i < count; ++i) {
        ParamInfoABI info;
        provider->get_parameter_info_abi(i, &info);
        expected_types[info.name] = info.default_value.type;
    }

    // Apply values from Lua
    for (const auto& kv : p_conf) {
        if (!kv.first.is<std::string>()) continue;
        std::string key = kv.first.as<std::string>();
        if (key == "enabled") continue;

        sol::object val = kv.second;
        auto it = expected_types.find(key);
        if (it == expected_types.end()) continue;

        ParamType expected_type = it->second;
        ParamValueABI abi_val;
        abi_val.type = expected_type;

        try {
            if (expected_type == ParamType::TYPE_BOOL && val.is<bool>()) {
                abi_val.b_val = val.as<bool>();
                provider->set_parameter_abi(key.c_str(), &abi_val);
            } 
            else if (expected_type == ParamType::TYPE_INT && val.is<double>()) {
                abi_val.i_val = val.as<int>();
                provider->set_parameter_abi(key.c_str(), &abi_val);
            } 
            else if (expected_type == ParamType::TYPE_FLOAT && val.is<double>()) {
                abi_val.f_val = static_cast<float>(val.as<double>());
                provider->set_parameter_abi(key.c_str(), &abi_val);
            }
            else if (expected_type == ParamType::TYPE_VEC3 && val.is<sol::table>()) {
                sol::table t = val.as<sol::table>();
                if (t.size() >= 3) {
                    abi_val.vec3_val[0] = t.get_or(1, 0.0f);
                    abi_val.vec3_val[1] = t.get_or(2, 0.0f);
                    abi_val.vec3_val[2] = t.get_or(3, 0.0f);
                    provider->set_parameter_abi(key.c_str(), &abi_val);
                }
            }
            else if (expected_type == ParamType::TYPE_STRING && val.is<std::string>()) {
                std::string lua_str = val.as<std::string>();
                std::strncpy(abi_val.s_val, lua_str.c_str(), 255);
                abi_val.s_val[255] = '\0';
                provider->set_parameter_abi(key.c_str(), &abi_val);
            }
        } catch (const sol::error& e) {
            std::cerr << "Lua Type Error for provider parameter '" << key << "': " << e.what() << std::endl;
        }
    }

    return true;
}

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

    // --- 5. ДИНАМИЧЕСКОЕ УПРАВЛЕНИЕ ПАРАМЕТРАМИ ЭФФЕКТОВ (PER-FRAME) ---
    // [ИСПРАВЛЕНО]: Вынесено на уровень модуля, а не внутрь лямбды set_interval!
    core_table["set_effect_param"] = [this](const std::string& output_name, const std::string& param_name, sol::object val) {
        if (!get_effect_for_output) return;
        
        IWallpaperEffectABI* effect = get_effect_for_output(output_name);
        if (!effect) return;

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

        if (!found) return; 

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
                std::string lua_str = val.as<std::string>();
                std::strncpy(abi_val.s_val, lua_str.c_str(), 255);
                abi_val.s_val[255] = '\0';
            } else {
                return; 
            }
            
            effect->set_parameter_abi(param_name.c_str(), &abi_val);
            
        } catch (const sol::error& e) {
            std::cerr << "[Lua] Type error in set_effect_param: " << e.what() << std::endl;
        }
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

    res.effect_name = core.get_or("default_effect", core.get_or("active_effect", std::string("")));
    // Читаем глобальный лимит кадров (по умолчанию 0.0)
    res.fps_limit = core.get_or("fps_limit", 0.0f);

    sol::object outputs_obj = core["outputs"];
    if (!outputs_obj.is<sol::table>()) return res;
    sol::table outputs = outputs_obj.as<sol::table>();

    sol::object out_conf_obj = outputs[output_name];
    if (!out_conf_obj.valid() && !output_desc.empty()) {
        out_conf_obj = outputs[output_desc];
    }

    if (out_conf_obj.is<sol::table>()) {
        sol::table out_conf = out_conf_obj.as<sol::table>();
        res.effect_name = out_conf.get_or("effect", res.effect_name);
        res.fps_limit = out_conf.get_or("fps_limit", res.fps_limit); // Локальный лимит
        
        sol::table settings;
        if (out_conf["settings"].is<sol::table>()) {
            settings = out_conf["settings"];
            // Проверяем лимит еще и внутри таблицы settings
            res.fps_limit = settings.get_or("fps_limit", res.fps_limit);
        } else {
            settings = lua.create_table();
            out_conf["settings"] = settings;
        }
        
        std::string preset = out_conf.get_or("preset", std::string(""));
        std::string applied_preset = out_conf.get_or("_applied_preset", std::string(""));

        if (!preset.empty() && preset != applied_preset) {
            sol::function apply_preset = core["utils"]["apply_preset"];
            if (apply_preset.valid()) {
                apply_preset(settings, res.effect_name, preset);
                out_conf["_applied_preset"] = preset; // Ставим метку, что пресет уже применен!
            }
        }
        res.custom_settings = settings;
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
    std::cout << "[LuaEngine] Clearing active timers before reload..." << std::endl;
    clear_timers(); // Mandatory: clear system timers and epoll before destroying sol::state
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

    // Базовые настройки плагина из config["Effect Name"]
    sol::table base_settings = lua["config"][effect_name];
    
    std::map<std::string, ParamType> expected_types;
    uint32_t count = effect->get_parameter_count_abi();
    for (uint32_t i = 0; i < count; ++i) {
        ParamInfoABI info;
        effect->get_parameter_info_abi(i, &info);
        expected_types[info.name] = info.default_value.type;
    }

    for (const auto& [key, expected_type] : expected_types) {
        // КАСКАДНЫЙ ПОИСК:
        // 1. Проверяем, переопределен ли параметр для конкретного монитора
        sol::object val = sol::nil;
        if (output_specific_settings.valid()) {
            val = output_specific_settings[key];
        }
        // 2. Если нет — берем из глобального конфига плагина
        if (!val.valid() && base_settings.valid()) {
            val = base_settings[key];
        }
        // 3. Если и там нет — оставляем дефолтное значение C++
        if (!val.valid()) continue;

        ParamValueABI abi_val;
        abi_val.type = expected_type;
        std::string temp_str;

        try {
            if (expected_type == ParamType::TYPE_BOOL && val.is<bool>()) {
                abi_val.b_val = val.as<bool>();
                effect->set_parameter_abi(key.c_str(), &abi_val);
            } 
            else if (expected_type == ParamType::TYPE_INT && val.is<double>()) {
                abi_val.i_val = val.as<int>();
                effect->set_parameter_abi(key.c_str(), &abi_val);
            } 
            else if (expected_type == ParamType::TYPE_FLOAT && val.is<double>()) {
                abi_val.f_val = static_cast<float>(val.as<double>());
                effect->set_parameter_abi(key.c_str(), &abi_val);
            } 
            else if (expected_type == ParamType::TYPE_VEC3 && val.is<sol::table>()) {
                sol::table t = val.as<sol::table>();
                if (t.size() >= 3) {
                    abi_val.vec3_val[0] = t.get_or(1, 0.0f);
                    abi_val.vec3_val[1] = t.get_or(2, 0.0f);
                    abi_val.vec3_val[2] = t.get_or(3, 0.0f);
                    effect->set_parameter_abi(key.c_str(), &abi_val);
                }
            }
            else if (expected_type == ParamType::TYPE_STRING && val.is<std::string>()) {
                std::string lua_str = val.as<std::string>();
                std::strncpy(abi_val.s_val, lua_str.c_str(), 255);
                abi_val.s_val[255] = '\0';
                effect->set_parameter_abi(key.c_str(), &abi_val);
            }
        } catch (const sol::error& e) {
            std::cerr << "Lua Type Error for parameter '" << key << "': " << e.what() << std::endl;
        }
    }
}
