#!/usr/bin/env python3
import re
import argparse
import json
from pathlib import Path
from typing import List, Dict, Any, Tuple

# Mapping from GLSL types to C++ equivalents and OpenGL functions
TYPE_MAP = {
    'float': {
        'cpp_type': 'float',
        'variant_type': 'float',
        'gl_uniform_func': 'glUniform1f',
        'default_value_parser': float,
    },
    'int': {
        'cpp_type': 'int',
        'variant_type': 'int',
        'gl_uniform_func': 'glUniform1i',
        'default_value_parser': int,
    },
    'bool': {
        'cpp_type': 'bool',
        'variant_type': 'bool',
        'gl_uniform_func': 'glUniform1i',
        'default_value_parser': lambda v: v.lower() in ['true', '1'],
    },
    'vec2': {
        'cpp_type': 'glm::vec2',
        'variant_type': 'glm::vec2',
        'gl_uniform_func': 'glUniform2fv',
        'default_value_parser': lambda v: f'glm::vec2({", ".join(val.strip() + "f" for val in v.split(","))})',
    },
    'vec3': {
        'cpp_type': 'glm::vec3',
        'variant_type': 'glm::vec3',
        'gl_uniform_func': 'glUniform3fv',
        'default_value_parser': lambda v: f'glm::vec3({", ".join(val.strip() + "f" for val in v.split(","))})',
    },
    'vec4': {
        'cpp_type': 'glm::vec4',
        'variant_type': 'glm::vec4',
        'gl_uniform_func': 'glUniform4fv',
        'default_value_parser': lambda v: f'glm::vec4({", ".join(val.strip() + "f" for val in v.split(","))})',
    },
    'string': {
        'cpp_type': 'std::string',
        'variant_type': 'std::string',
        'gl_uniform_func': None, # Strings are not sent to GLSL as uniforms
        'default_value_parser': lambda v: f'"{v.strip()}"',
    },
}

def parse_shader_params(shader_content: str) -> Tuple[List[Dict[str, Any]], List[Dict[str, Any]]]:
    """Parses the shader content to find parameter and texture comments."""
    params = []
    textures = []
    
    # Parse standard parameters
    param_regex = re.compile(
        r"//\s*@param\s+([a-zA-Z0-9_]+)\s*\|\s*(float|int|bool|vec2|vec3|vec4|string)\s*\|\s*([^|]+)\s*\|\s*(.*)"
    )

    for match in param_regex.finditer(shader_content):
        name, type_str, default_str, description = match.groups()
        if type_str not in TYPE_MAP:
            print(f"Warning: Unsupported type '{type_str}' for param '{name}'. Skipping.")
            continue
        
        type_info = TYPE_MAP[type_str]
        try:
            default_value = type_info['default_value_parser'](default_str.strip())
            params.append({
                'name': name.strip(),
                'type': type_str,
                'default_value': default_value,
                'description': description.strip(),
                'type_info': type_info,
            })
        except (ValueError, TypeError) as e:
            print(f"Error: Could not parse default value '{default_str}' for '{name}'. Error: {e}")
            continue

    # Parse textures: // @texture u_name | default.jpg | Description
    tex_regex = re.compile(r"//\s*@texture\s+([a-zA-Z0-9_]+)\s*\|\s*([^|]+)\s*\|\s*(.*)")
    for i, match in enumerate(tex_regex.finditer(shader_content)):
        name, default_path, description = match.groups()
        textures.append({
            'name': name.strip(),
            'default_path': default_path.strip(),
            'description': description.strip(),
            'unit': i  # Texture unit (0, 1, 2...)
        })
            
    return params, textures

def find_shaders(shader_dir: Path) -> Tuple[Path | None, Path | None]:
    """Finds vertex and fragment shaders in a directory."""
    vert_shader, frag_shader = None, None
    if not shader_dir.is_dir():
        return None, None
        
    for f in shader_dir.iterdir():
        if 'vert' in f.name:
            vert_shader = f
        elif 'frag' in f.name:
            frag_shader = f
    
    if not vert_shader:
        print("Info: No vertex shader (.vert, .glsl) found. Will use a default fullscreen quad vertex shader.")
    if not frag_shader:
        print("Error: Fragment shader (.frag) is required but not found.")
        return None, None
        
    return vert_shader, frag_shader

def generate_code_snippets(params: List[Dict[str, Any]], textures: List[Dict[str, Any]]) -> Dict[str, str]:
    """Generates various C++ code snippets from parsed parameters and textures."""
    snippets = {
        "MEMBER_DECLARATIONS": [], "UNIFORM_DECLARATIONS": [],
        "GET_PARAMETERS_IMPL": [], "SET_PARAMETER_IMPL": [],
        "GET_UNIFORM_LOCS_IMPL": [], "SET_UNIFORMS_IMPL": [],
        "TEX_MEMBERS": [], "TEX_CLEANUP": [], "TEX_RENDER_BIND": [], "STB_INCLUDE": []
    }

    # --- 1. Standard parameters ---
    for i, p in enumerate(params):
        default = p['default_value']
        if p['type'] == 'bool': default = 'true' if default else 'false'
        
        snippets["MEMBER_DECLARATIONS"].append(f"    {p['type_info']['cpp_type']} {p['name']} = {default};")
        snippets["GET_PARAMETERS_IMPL"].append(f'        {{"{p["name"]}", "{p["description"]}", {p["name"]}}},')
        
        # BUGFIX: First parameter is always 'if', subsequent are 'else if'
        if_stmt = "if" if i == 0 else "else if"
        snippets["SET_PARAMETER_IMPL"].append(
            f'        {if_stmt} (name == "{p["name"]}") {{\n'
            f'            {p["name"]} = std::get<{p["type_info"]["variant_type"]}>(value);\n'
            f'        }}'
        )
        
        # Strings are not sent to GLSL
        if p['type_info']['gl_uniform_func'] is not None:
            snippets["UNIFORM_DECLARATIONS"].append(f"    GLuint u_{p['name']} = 0;")
            snippets["GET_UNIFORM_LOCS_IMPL"].append(f'    u_{p["name"]} = glGetUniformLocation(program, "{p["name"]}");')
            
            args = f'u_{p["name"]}, {p["name"]}'
            if 'vec' in p['type']: args = f'u_{p["name"]}, 1, &{p["name"]}[0]'
            snippets["SET_UNIFORMS_IMPL"].append(f"    if (u_{p['name']} != -1) {p['type_info']['gl_uniform_func']}({args});")

    # --- 2. Textures ---
    for i, t in enumerate(textures):
        n = t['name']
        
        # Class member variables for the texture
        snippets["TEX_MEMBERS"].append(
            f"    GLuint {n}_id = 0;\n"
            f"    std::string {n}_path = \"{t['default_path']}\";\n"
            f"    bool {n}_pending = true;\n"
            f"    float {n}_w = 1.0f;\n"
            f"    float {n}_h = 1.0f;"
        )
        
        # Uniforms (Sampler and image resolution)
        snippets["UNIFORM_DECLARATIONS"].append(f"    GLuint u_{n} = 0;\n    GLuint u_{n}_resolution = 0;")
        snippets["GET_UNIFORM_LOCS_IMPL"].append(
            f'    u_{n} = glGetUniformLocation(program, "{n}");\n'
            f'    u_{n}_resolution = glGetUniformLocation(program, "{n}_resolution");'
        )
        
        # Export to Lua as a string (file path)
        snippets["GET_PARAMETERS_IMPL"].append(f'        {{"{n}_path", "{t["description"]}", {n}_path}},')
        
        # BUGFIX: First texture is 'if' ONLY if there were no standard params before it
        if_stmt = "if" if (i == 0 and not params) else "else if"
        snippets["SET_PARAMETER_IMPL"].append(
            f'        {if_stmt} (name == "{n}_path") {{\n'
            f'            {n}_path = std::get<std::string>(value);\n'
            f'            {n}_pending = true;\n'
            f'        }}'
        )
        
        # Binding before rendering
        snippets["TEX_RENDER_BIND"].append(
            f"    if ({n}_id) {{\n"
            f"        glActiveTexture(GL_TEXTURE0 + {t['unit']});\n"
            f"        glBindTexture(GL_TEXTURE_2D, {n}_id);\n"
            f"        if (u_{n} != -1) glUniform1i(u_{n}, {t['unit']});\n"
            f"        if (u_{n}_resolution != -1) glUniform2f(u_{n}_resolution, {n}_w, {n}_h);\n"
            f"    }}"
        )
        
        snippets["TEX_CLEANUP"].append(f"    if ({n}_id) {{ glDeleteTextures(1, &{n}_id); {n}_id = 0; }}")

    if textures:
        snippets["STB_INCLUDE"].append('#define STB_IMAGE_IMPLEMENTATION\n#include "stb_image.h"')

    if snippets["SET_PARAMETER_IMPL"]:
        snippets["SET_PARAMETER_IMPL"].append(
            '        else {\n'
            '             std::cerr << "Warning: Unknown parameter \'" << name << "\'." << std::endl;\n'
            '        }'
        )
        
    return {key: '\n'.join(value) for key, value in snippets.items()}

def fill_template(template_content: str, placeholders: Dict[str, str]) -> str:
    """Fills placeholders in a template string."""
    for key, value in placeholders.items():
        template_content = template_content.replace(f'{{{{{key}}}}}', str(value))
    return template_content

def main():
    parser = argparse.ArgumentParser(description="Generates C++ standalone plugin files from annotated GLSL shaders.")
    parser.add_argument("plugin_dir", type=Path, help="Path to the plugin directory. Should contain a 'shaders/' subdirectory.")
    parser.add_argument("--list-params", action="store_true", help="List shader parameters as JSON and exit.")
    args = parser.parse_args()

    if not args.plugin_dir.is_dir():
        print(f"Error: Directory not found at '{args.plugin_dir}'")
        return

    # --- Find and Parse Shaders ---
    shader_dir = args.plugin_dir / "shaders"
    vert_shader_path, frag_shader_path = find_shaders(shader_dir)

    if not frag_shader_path:
        return # error message already printed in find_shaders

    shader_content = ""
    try:
        if vert_shader_path:
            shader_content += vert_shader_path.read_text(encoding='utf-8')
        shader_content += frag_shader_path.read_text(encoding='utf-8')
        params, textures = parse_shader_params(shader_content)
    except Exception as e:
        print(f"Error reading shader files: {e}")
        return
        
    # --- Handle --list-params ---
    if args.list_params:
        params_for_json = [{k: v for k, v in p.items() if k != 'type_info'} for p in params]
        print(json.dumps(params_for_json, indent=2, ensure_ascii=False))
        return

    # --- Define names and paths ---
    project_name = args.plugin_dir.name
    class_name = ''.join(word.capitalize() for word in re.split('[-_]', project_name)) + "Effect"
    effect_name = project_name.replace('-', ' ').title()
    
    placeholders = {
        "PROJECT_NAME": project_name,
        "CLASS_NAME": class_name,
        "EFFECT_NAME": effect_name,
        "HEADER_GUARD": f"{project_name.upper().replace('-', '_')}_HPP",
        "HEADER_FILENAME": f"{project_name}.hpp",
        "CPP_FILENAME": f"{project_name}.cpp",
        "VERT_SHADER_FILENAME": vert_shader_path.name if vert_shader_path else "common_vert.glsl",
        "FRAG_SHADER_FILENAME": frag_shader_path.name,
    }
    
    # --- Resolve Template Directory ---
    local_templates = Path(__file__).parent / "templates"
    system_templates = Path("/usr/share/shader-desk/templates")
    
    if local_templates.is_dir():
        templates_dir = local_templates
    elif system_templates.is_dir():
        templates_dir = system_templates
    else:
        print("Error: Templates directory not found! Expected at local ./templates or /usr/share/shader-desk/templates")
        return

    # --- Generate Code and Fill Templates ---
    code_snippets = generate_code_snippets(params, textures)
    placeholders.update(code_snippets)

    # --- Generate Texture Processing Logic ---
    tex_proc_code = ""
    if textures:
        tex_proc_code = f"\nvoid {class_name}::process_textures() {{\n    if (!m_core) return;\n"
        for t in textures:
            n = t['name']
            tex_proc_code += f"""
    if ({n}_pending && !{n}_path.empty()) {{
        {n}_pending = false;
        if ({n}_id) glDeleteTextures(1, &{n}_id);
        
        // Resolve absolute path or bundle-relative path
        std::string fp = {n}_path;
        if (fp[0] != '/') {{
            fp = std::string(m_core->get_bundle_path(get_name())) + "/" + fp;
        }}
        
        int w, h, c;
        stbi_set_flip_vertically_on_load(true);
        unsigned char* d = stbi_load(fp.c_str(), &w, &h, &c, 4); // Force RGBA
        
        if (d) {{
            glGenTextures(1, &{n}_id);
            glBindTexture(GL_TEXTURE_2D, {n}_id);
            
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, d);
            glGenerateMipmap(GL_TEXTURE_2D);
            
            {n}_w = static_cast<float>(w);
            {n}_h = static_cast<float>(h);
            stbi_image_free(d);
            std::cout << "[" << get_name() << "] Loaded texture: " << fp << "\\n";
        }} else {{
            std::cerr << "[" << get_name() << "] Failed to load texture: " << fp << "\\n";
        }}
    }}"""
        tex_proc_code += "\n}\n"

    placeholders["TEX_PROCESS_IMPL"] = tex_proc_code
    placeholders["TEX_PROCESS_CALL"] = "    process_textures();" if textures else ""
    placeholders["TEX_PROCESS_DECL"] = "    void process_textures();" if textures else ""

    files_to_generate = {
        "plugin.hpp.template": args.plugin_dir / placeholders["HEADER_FILENAME"],
        "plugin.cpp.template": args.plugin_dir / placeholders["CPP_FILENAME"],
        "CMakeLists.txt.template": args.plugin_dir / "CMakeLists.txt",
    }
    
    print(f"Generating standalone plugin '{effect_name}' in directory '{args.plugin_dir}'...")

    for template_name, output_path in files_to_generate.items():
        try:
            template_content = (templates_dir / template_name).read_text(encoding='utf-8')
            generated_content = fill_template(template_content, placeholders)
            output_path.write_text(generated_content, encoding='utf-8')
            print(f"  ✓ Created {output_path}")
        except Exception as e:
            print(f"  ✗ Error generating {output_path}: {e}")

    # --- Generate Default Preset ---
    preset_dir = args.plugin_dir / "presets"
    preset_dir.mkdir(exist_ok=True)
    default_preset_file = preset_dir / "default.lua"
    
    if not default_preset_file.exists():
        try:
            with open(default_preset_file, 'w', encoding='utf-8') as f:
                f.write(f"-- Default preset for {effect_name}\n")
                f.write("return {\n")
                # Textures
                for t in textures:
                    f.write(f"    {t['name']}_path = \"{t['default_path']}\", -- {t['description']}\n")
                # Params
                for p in params:
                    val = p['default_value']
                    if p['type'] == 'bool':
                        val = 'true' if val else 'false'
                    elif p['type'] == 'string':
                        val = f'"{val}"'
                    elif 'vec' in p['type']:
                        val = str(val).replace('glm::vec', '{').replace('(', '').replace(')', '}').replace('f', '')
                    f.write(f"    {p['name']} = {val}, -- {p['description']}\n")
                f.write("}\n")
            print(f"  ✓ Created default preset: {default_preset_file}")
        except Exception as e:
            print(f"  ✗ Error generating default preset: {e}")

    print("\nGeneration complete!")
    print(f"Next step: Run 'cd {args.plugin_dir} && mkdir build && cd build && cmake .. && make' to compile your plugin.")

if __name__ == "__main__":
    main()