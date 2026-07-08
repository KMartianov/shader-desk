#include "shadertoy-effect.hpp"
#include <shader-desk/shader-utils.hpp>
#include <iostream>
#include <regex>
#include <sstream>

// ==============================================================================
// 1. ПАРСИНГ ПАРАМЕТРОВ ИЗ КОММЕНТАРИЕВ GLSL
// ==============================================================================
EffectParameterValue ShaderToyEffect::parse_default_value(const std::string& type_str, const std::string& val_str) {
    if (type_str == "float") return std::stof(val_str);
    if (type_str == "int") return std::stoi(val_str);
    if (type_str == "bool") return (val_str == "true" || val_str == "1");
    if (type_str == "vec3") {
        glm::vec3 v(0.0f);
        sscanf(val_str.c_str(), "%f,%f,%f", &v.x, &v.y, &v.z);
        return v;
    }
    return 0.0f;
}

void ShaderToyEffect::extract_parameters_from_source(const std::string& source) {
    dynamic_params.clear();
    // Ищем паттерн: // @param name | type | default | description
    std::regex param_regex(R"(//\s*@param\s+([a-zA-Z0-9_]+)\s*\|\s*(float|int|bool|vec3)\s*\|\s*([^|]+)\s*\|\s*(.*))");
    
    auto words_begin = std::sregex_iterator(source.begin(), source.end(), param_regex);
    auto words_end = std::sregex_iterator();

    for (std::sregex_iterator i = words_begin; i != words_end; ++i) {
        std::smatch match = *i;
        DynamicParam param;
        param.name = match[1].str();
        std::string type_str = match[2].str();
        std::string default_str = match[3].str();
        param.description = match[4].str();
        
        // Очищаем от пробелов по краям
        default_str.erase(0, default_str.find_first_not_of(" \t"));
        default_str.erase(default_str.find_last_not_of(" \t") + 1);

        try {
            param.value = parse_default_value(type_str, default_str);
            dynamic_params.push_back(param);
        } catch (...) {
            std::cerr << "[ShaderToy] Failed to parse default value for '" << param.name << "'\n";
        }
    }
}

// ==============================================================================
// 2. ИНЖЕКЦИЯ КОДА И КОМПИЛЯЦИЯ
// ==============================================================================
bool ShaderToyEffect::parse_and_compile(ICoreContext* core) {
    std::string raw_frag = shader_utils::load_shader_source(core, get_name(), target_shader_file);
    if (raw_frag.empty()) return false;

    // 1. Извлекаем метаданные параметров
    extract_parameters_from_source(raw_frag);

    // 2. Генерируем "Обертку" ShaderToy
    std::stringstream injected_frag;
    injected_frag << "#version 300 es\n";
    injected_frag << "precision highp float;\n";
    injected_frag << "out vec4 FragColor;\n";
    
    // Встроенные Uniforms ShaderToy
    injected_frag << "uniform vec3 iResolution;\n";
    injected_frag << "uniform float iTime;\n";
    injected_frag << "uniform float iTimeDelta;\n";
    injected_frag << "uniform int iFrame;\n";
    injected_frag << "uniform vec4 iMouse;\n";

    // Динамические Uniforms пользователя
    for (const auto& p : dynamic_params) {
        if (std::holds_alternative<float>(p.value)) injected_frag << "uniform float " << p.name << ";\n";
        else if (std::holds_alternative<int>(p.value)) injected_frag << "uniform int " << p.name << ";\n";
        else if (std::holds_alternative<bool>(p.value)) injected_frag << "uniform bool " << p.name << ";\n";
        else if (std::holds_alternative<glm::vec3>(p.value)) injected_frag << "uniform vec3 " << p.name << ";\n";
    }

    // Вставляем оригинальный код пользователя
    injected_frag << "\n#line 1\n" << raw_frag << "\n";

    // Добавляем точку входа main(), которая вызывает ShaderToy mainImage()
    injected_frag << R"(
    void main() {
        mainImage(FragColor, gl_FragCoord.xy);
    }
    )";

    // 3. Создаем простейший вершинный шейдер (Fullscreen Triangle без VBO)
    std::string vert_src = R"(
        #version 300 es
        void main() {
            float x = -1.0 + float((gl_VertexID & 1) << 2);
            float y = -1.0 + float((gl_VertexID & 2) << 1);
            gl_Position = vec4(x, y, 0.0, 1.0);
        }
    )";

    program = shader_utils::create_shader_program(vert_src, injected_frag.str());
    if (!program) {
        std::cerr << "[ShaderToy] Compilation failed!\n";
        return false;
    }

    // 4. Кэшируем locations
    u_iResolution = glGetUniformLocation(program, "iResolution");
    u_iTime = glGetUniformLocation(program, "iTime");
    u_iTimeDelta = glGetUniformLocation(program, "iTimeDelta");
    u_iFrame = glGetUniformLocation(program, "iFrame");
    u_iMouse = glGetUniformLocation(program, "iMouse");

    for (auto& p : dynamic_params) {
        p.uniform_location = glGetUniformLocation(program, p.name.c_str());
    }

    return true;
}

// ==============================================================================
// 3. CORE API
// ==============================================================================
bool ShaderToyEffect::initialize(ICoreContext* core, uint32_t width, uint32_t height) {
    if (program) return true;

    p_mouse_x = core->get_blackboard()->bind_float("mouse.accum_x");
    p_mouse_y = core->get_blackboard()->bind_float("mouse.accum_y");

    if (!parse_and_compile(core)) return false;

    glGenVertexArrays(1, &vao);
    std::cout << "[ShaderToy] Successfully loaded Sandbox! Params exported: " << dynamic_params.size() << "\n";
    return true;
}

void ShaderToyEffect::render(uint32_t width, uint32_t height, float dt) {
    glUseProgram(program);
    glDisable(GL_DEPTH_TEST);

    time_accum += dt;
    frame_count++;

    // Standard ShaderToy uniforms
    if (u_iResolution != -1) glUniform3f(u_iResolution, static_cast<float>(width), static_cast<float>(height), 1.0f);
    if (u_iTime != -1) glUniform1f(u_iTime, time_accum);
    if (u_iTimeDelta != -1) glUniform1f(u_iTimeDelta, dt);
    if (u_iFrame != -1) glUniform1i(u_iFrame, frame_count);
    
    if (u_iMouse != -1) {
        float mx = p_mouse_x ? *p_mouse_x : 0.0f;
        float my = p_mouse_y ? *p_mouse_y : 0.0f;
        glUniform4f(u_iMouse, mx, my, 0.0f, 0.0f); // xy - позиция
    }

    // Dynamic User uniforms (Передаем в видеокарту без поиска по Map)
    for (const auto& p : dynamic_params) {
        if (p.uniform_location == -1) continue;

        if (std::holds_alternative<float>(p.value)) 
            glUniform1f(p.uniform_location, std::get<float>(p.value));
        else if (std::holds_alternative<int>(p.value)) 
            glUniform1i(p.uniform_location, std::get<int>(p.value));
        else if (std::holds_alternative<bool>(p.value)) 
            glUniform1i(p.uniform_location, std::get<bool>(p.value) ? 1 : 0);
        else if (std::holds_alternative<glm::vec3>(p.value)) {
            glm::vec3 v = std::get<glm::vec3>(p.value);
            glUniform3f(p.uniform_location, v.x, v.y, v.z);
        }
    }

    // Отрисовка
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

void ShaderToyEffect::cleanup() {
    if (program) glDeleteProgram(program);
    if (vao) glDeleteVertexArrays(1, &vao);
    program = 0; vao = 0;
}

// ==============================================================================
// 4. ДИНАМИЧЕСКИЙ ПРОБРОС ПАРАМЕТРОВ В LUA
// ==============================================================================
std::vector<EffectParameter> ShaderToyEffect::get_parameters() const {
    std::vector<EffectParameter> exports;
    // Обязательный параметр (позволяет менять файл шейдера из Lua на лету!)
    exports.push_back({"shader_file", "File name in shaders/ folder", target_shader_file});
    
    for (const auto& p : dynamic_params) {
        exports.push_back({p.name, p.description, p.value});
    }
    return exports;
}

void ShaderToyEffect::set_parameter(const std::string& name, const EffectParameterValue& value) {
    if (name == "shader_file") {
        target_shader_file = std::get<std::string>(value);
        return;
    }

    // Ищем динамический параметр и обновляем его
    for (auto& p : dynamic_params) {
        if (p.name == name) {
            p.value = value;
            break;
        }
    }
}

// ABI Экспорт
extern "C" {
    uint32_t get_abi_version() { return SHADER_DESK_ABI_VERSION; }
    IWallpaperEffectABI* create_effect() { return new ShaderToyEffect(); }
    void destroy_effect(IWallpaperEffectABI* effect) { delete static_cast<WallpaperEffect*>(effect); }
}