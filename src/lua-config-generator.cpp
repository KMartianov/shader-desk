// src/lua-config-generator.cpp
#include "lua-config-generator.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <map>

namespace fs = std::filesystem;

// ==============================================================================
// ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ
// ==============================================================================

std::string LuaConfigGenerator::get_config_dir() {
    const char* xdg_config = std::getenv("XDG_CONFIG_HOME");
    std::string base_dir;
    if (xdg_config && *xdg_config) {
        base_dir = std::string(xdg_config);
    } else {
        base_dir = std::string(std::getenv("HOME")) + "/.config";
    }
    return base_dir + "/interactive-wallpaper";
}

// Превращает "Ico Sphere Effect" в "ico_sphere_effect"
std::string LuaConfigGenerator::sanitize_filename(std::string name) {
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
        if (std::isspace(c) || c == '-') return '_';
        return (char)std::tolower(c);
    });
    return name;
}

// Конвертирует C-ABI значения (ParamValueABI) в синтаксис языка Lua
// [NEW] Эта функция теперь работает исключительно с плоскими структурами C, а не с std::variant.
std::string LuaConfigGenerator::value_to_lua_string(const ParamValueABI& val) {
    switch (val.type) {
        case ParamType::TYPE_BOOL: 
            return val.b_val ? "true" : "false";
        case ParamType::TYPE_INT: 
            return std::to_string(val.i_val);
        case ParamType::TYPE_FLOAT: 
            return std::to_string(val.f_val);
        case ParamType::TYPE_VEC3: 
            return "{" + std::to_string(val.vec3_val[0]) + ", " + 
                         std::to_string(val.vec3_val[1]) + ", " + 
                         std::to_string(val.vec3_val[2]) + "}";
        case ParamType::TYPE_STRING: 
            return std::string("\"") + val.s_val + "\""; // Оборачиваем в кавычки для Lua
    }
    return "nil";
}

// Удаляет пробелы по краям строки
static std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t");
    if (std::string::npos == first) return "";
    size_t last = str.find_last_not_of(" \t");
    return str.substr(first, (last - first + 1));
}

// ==============================================================================
// ОСНОВНАЯ ЛОГИКА ГЕНЕРАЦИИ
// ==============================================================================

void LuaConfigGenerator::generate_configs(PluginManager& pm) {
    std::string config_dir = get_config_dir();
    fs::path plugins_dir = fs::path(config_dir) / "plugins";

    // Создаем директории, если их нет
    fs::create_directories(plugins_dir);

    auto available_effects = pm.get_available_effects();
    if (available_effects.empty()) {
        std::cerr << "Warning: No visual plugins found. Nothing to configure." << std::endl;
        return;
    }

    std::cout << "Generating Lua configs in: " << config_dir << std::endl;

    // 1. Создаем главный init.lua (Только если его нет! Мы никогда не затираем логику пользователя)
    fs::path init_lua_path = fs::path(config_dir) / "init.lua";
    if (!fs::exists(init_lua_path)) {
        generate_init_lua(init_lua_path.string(), available_effects[0]);
    }

    // 2. Итерируемся по плагинам и обновляем/создаем их индивидуальные конфиги
    for (const auto& effect_name : available_effects) {
        auto effect = pm.create_effect(effect_name);
        if (!effect) continue;

        std::string filename = sanitize_filename(effect_name) + ".lua";
        fs::path plugin_filepath = plugins_dir / filename;

        update_plugin_config(plugin_filepath.string(), effect_name, effect.get());
        std::cout << "  ✓ Processed config for: " << effect_name << std::endl;
    }

    std::cout << "Config generation completed successfully!" << std::endl;
}

void LuaConfigGenerator::generate_init_lua(const std::string& filepath, const std::string& default_effect) {
    std::ofstream out(filepath);
    out << "-- ~/.config/interactive-wallpaper/init.lua\n";
    out << "-- Это ГЛАВНЫЙ конфигурационный файл. Сюда можно писать любую Lua логику.\n\n";
    
    out << "-- 1. Настройки Ядра\n";
    out << "core = core or {}\n";
    out << "core.active_effect = \"" << default_effect << "\"\n";
    out << "core.interactive = true\n\n";

    out << "-- 2. Настройки Провайдеров Данных (Smart Providers)\n";
    out << "core.providers = {\n";
    out << "    [\"Evdev Pointer Provider\"] = {\n";
    out << "        enabled = true,\n";
    out << "        mouse_sensitivity = 1.0,\n";
    out << "        touchpad_sensitivity = 3.0,\n";
    out << "        invert_x = false,\n";
    out << "        invert_y = false\n";
    out << "    },\n";
    out << "    [\"Cava Audio Provider\"] = {\n";
    out << "        enabled = true,\n";
    out << "        smoothing = 0.85,\n";
    out << "        volume_multiplier = 1.0,\n";
    out << "        bass_multiplier = 1.0,\n";
    out << "        mid_multiplier = 1.0,\n";
    out << "        treble_multiplier = 1.0\n";
    out << "    }\n";
    out << "}\n\n";

    out << "-- 3. Переопределение настроек плагинов (Свободная логика)\n";
    out << "-- Файлы плагинов из папки `plugins/` загружаются автоматически ДО этого файла.\n";
    out << "-- Поэтому вы можете безопасно переопределять их значения здесь.\n";
    out.close();
}

void LuaConfigGenerator::update_plugin_config(const std::string& filepath, const std::string& plugin_name, IWallpaperEffectABI* effect) {
    std::map<std::string, std::string> user_values;
    std::vector<std::string> user_logic;

    // --- ШАГ 1: Парсинг существующего файла (если есть) ---
    if (fs::exists(filepath)) {
        std::ifstream in(filepath);
        std::string line;
        bool in_managed = false;
        
        while (std::getline(in, line)) {
            if (line.find("<<< BEGIN MANAGED PARAMS >>>") != std::string::npos) { 
                in_managed = true; 
                continue; 
            }
            if (line.find("<<< END MANAGED PARAMS >>>") != std::string::npos) { 
                in_managed = false; 
                continue; 
            }

            if (in_managed) {
                // Ищем конструкцию вида: p.key = value -- comment
                // Это примитивный, но надежный парсер для наших нужд.
                auto dot_pos = line.find("p.");
                auto eq_pos = line.find("=");
                
                if (dot_pos != std::string::npos && eq_pos != std::string::npos && dot_pos < eq_pos) {
                    std::string key = trim(line.substr(dot_pos + 2, eq_pos - (dot_pos + 2)));
                    std::string rest = line.substr(eq_pos + 1);
                    
                    // Отрезаем комментарии от значения (ищем '--')
                    auto comment_pos = rest.find("--");
                    std::string value;
                    if (comment_pos != std::string::npos) {
                        value = trim(rest.substr(0, comment_pos));
                    } else {
                        value = trim(rest);
                    }
                    
                    // Если значение заканчивается запятой (случайность), отрезаем её
                    if (!value.empty() && value.back() == ',') value.pop_back();

                    user_values[key] = value;
                }
            } else {
                // Сохраняем свободную логику пользователя (пропуская шаблонный заголовок)
                if (line.find("config[\"") == std::string::npos && line.find("local p =") == std::string::npos) {
                    user_logic.push_back(line);
                }
            }
        }
    }

    // --- ШАГ 2: Генерация обновленного файла ---
    std::ofstream out(filepath);
    
    // Стандартный заголовок
    out << "config[\"" << plugin_name << "\"] = config[\"" << plugin_name << "\"] or {}\n";
    out << "local p = config[\"" << plugin_name << "\"]\n\n";
    
    out << "-- ==============================================================================\n";
    out << "-- <<< BEGIN MANAGED PARAMS >>>\n";
    out << "-- Не удаляйте маркеры. Изменяйте значения справа от знака '='.\n";
    out << "-- ==============================================================================\n";

    // Записываем актуальные параметры из C++
    // [NEW] Используем безопасный C-ABI для получения списка параметров плагина.
    uint32_t count = effect->get_parameter_count();
    for (uint32_t i = 0; i < count; ++i) {
        ParamInfoABI param_info;
        effect->get_parameter_info(i, &param_info);
        
        std::string default_val = value_to_lua_string(param_info.default_value);
        std::string param_name(param_info.name);

        out << "p." << param_name << " = ";
        
        if (user_values.count(param_name)) {
            // Восстанавливаем то, что написал пользователь
            out << user_values[param_name] << " -- " << param_info.description << "\n";
            user_values.erase(param_name); // Удаляем, чтобы найти DEPRECATED
        } else {
            // Вставляем новое дефолтное значение
            out << default_val << " -- [NEW] " << param_info.description << "\n";
        }
    }

    // То, что осталось в user_values, больше не существует в плагине (разработчик удалил или переименовал)
    for (const auto& [dep_key, dep_val] : user_values) {
        // Защита от дублирования маркера DEPRECATED
        if (dep_key.find("[DEPRECATED]") == std::string::npos) {
            out << "-- [DEPRECATED] p." << dep_key << " = " << dep_val << " -- Удалено в новой версии\n";
        }
    }

    out << "-- <<< END MANAGED PARAMS >>>\n";
    out << "-- ==============================================================================\n";

    // --- ШАГ 3: Восстановление пользовательской свободной логики ---
    // Убираем пустые строки в начале сохраненной логики
    while (!user_logic.empty() && trim(user_logic.front()).empty()) {
        user_logic.erase(user_logic.begin());
    }

    if (!user_logic.empty()) {
        for (const auto& line : user_logic) {
            out << line << "\n";
        }
    } else {
        out << "\n-- ЗОНА СВОБОДНОЙ ЛОГИКИ ПЛАГИНА:\n";
        out << "-- Пишите здесь функции и переопределения, специфичные для этого плагина.\n";
        out << "-- Пример загрузки пресета (раскомментируйте, если пресет существует):\n";
        out << "-- core.utils.apply_preset(p, \"" << plugin_name << "\", \"default\")\n";
    }

    out.close();
}