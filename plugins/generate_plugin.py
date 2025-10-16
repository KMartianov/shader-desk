#!/usr/bin/env python3
import re
import os
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
        'gl_uniform_func': 'glUniform1i',  # Bools are sent as integers
        'default_value_parser': lambda v: v.lower() in ['true', '1'],
    },
    'vec3': {
        'cpp_type': 'glm::vec3',
        'variant_type': 'glm::vec3',
        'gl_uniform_func': 'glUniform3fv',
        'default_value_parser': lambda v: f'glm::vec3({", ".join(val.strip() + "f" for val in v.split(","))})',
    },
}

def parse_shader_params(shader_content: str) -> List[Dict[str, Any]]:
    """Parses the shader content to find parameter comments."""
    params = []
    param_regex = re.compile(
        r"//\s*@param\s+([a-zA-Z0-9_]+)\s*\|\s*(float|int|bool|vec3)\s*\|\s*([^|]+)\s*\|\s*(.*)"
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
            
    return params

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
            print(f.name)
    
    if not vert_shader:
        print("Info: No vertex shader (.vert, .glsl) found. Will use a default.")
    if not frag_shader:
        print("Error: Fragment shader (.frag) is required but not found.")
        return None, None
        
    return vert_shader, frag_shader

def generate_code_snippets(params: List[Dict[str, Any]]) -> Dict[str, str]:
    """Generates various C++ code snippets from parsed parameters."""
    snippets = {
        "MEMBER_DECLARATIONS": [], "UNIFORM_DECLARATIONS": [],
        "GET_PARAMETERS_IMPL": [], "SET_PARAMETER_IMPL": [],
        "GET_UNIFORM_LOCS_IMPL": [], "SET_UNIFORMS_IMPL": [],
    }

    for i, p in enumerate(params):
        default = p['default_value']
        if p['type'] == 'bool': default = 'true' if default else 'false'
        snippets["MEMBER_DECLARATIONS"].append(f"    {p['type_info']['cpp_type']} {p['name']} = {default};")
        snippets["UNIFORM_DECLARATIONS"].append(f"    GLuint u_{p['name']} = 0;")
        snippets["GET_PARAMETERS_IMPL"].append(f'        {{"{p["name"]}", "{p["description"]}", {p["name"]}}},')
        
        if_stmt = "if" if i == 0 else "else if"
        snippets["SET_PARAMETER_IMPL"].append(
            f'        {if_stmt} (name == "{p["name"]}") {{\n'
            f'            {p["name"]} = std::get<{p["type_info"]["variant_type"]}>(value);\n'
            f'        }}'
        )
        snippets["GET_UNIFORM_LOCS_IMPL"].append(f'    u_{p["name"]} = glGetUniformLocation(program, "{p["name"]}");')
        
        args = f'u_{p["name"]}, {p["name"]}'
        if p['type'] == 'vec3': args = f'u_{p["name"]}, 1, &{p["name"]}[0]'
        snippets["SET_UNIFORMS_IMPL"].append(f"    {p['type_info']['gl_uniform_func']}({args});")

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
    parser = argparse.ArgumentParser(description="Generates C++ plugin files inside a plugin directory from annotated GLSL shaders.")
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
        return # Error message already printed in find_shaders

    shader_content = ""
    try:
        if vert_shader_path:
            shader_content += vert_shader_path.read_text(encoding='utf-8')
        shader_content += frag_shader_path.read_text(encoding='utf-8')
        params = parse_shader_params(shader_content)
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
    
    # --- Generate Code and Fill Templates ---
    code_snippets = generate_code_snippets(params)
    placeholders.update(code_snippets)
    
    script_dir = Path(__file__).parent
    templates_dir = script_dir / "templates"
    
    files_to_generate = {
        "plugin.hpp.template": args.plugin_dir / placeholders["HEADER_FILENAME"],
        "plugin.cpp.template": args.plugin_dir / placeholders["CPP_FILENAME"],
        "CMakeLists.txt.template": args.plugin_dir / "CMakeLists.txt",
    }
    
    print(f"Generating plugin '{effect_name}' in directory '{args.plugin_dir}'...")

    for template_name, output_path in files_to_generate.items():
        try:
            template_content = (templates_dir / template_name).read_text(encoding='utf-8')
            generated_content = fill_template(template_content, placeholders)
            output_path.write_text(generated_content, encoding='utf-8')
            print(f"  ✓ Created {output_path}")
        except Exception as e:
            print(f"  ✗ Error generating {output_path}: {e}")

    print("\nGeneration complete!")
    print(f"Next step: Add 'add_subdirectory({args.plugin_dir.name})' to your main plugins/CMakeLists.txt")

if __name__ == "__main__":
    main()