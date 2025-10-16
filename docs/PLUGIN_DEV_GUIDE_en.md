# Plugin Development Guide

This guide will help you create your own effect for `interactive-wallpaper`. The project architecture follows a "shader-first" principle: you write GLSL code, describe configurable parameters in comments, and a special script automatically generates the necessary C++ and CMake wrapper code.

This allows you to focus on the visual aspects while minimizing routine C++ work.

## ✨ Core Concept

1. **Create Directory**: Create a new folder for your plugin in the `plugins/` directory.
2. **Write GLSL Shader**: Write a fragment shader. All configurable parameters (e.g., color, speed, intensity) are declared as `uniform` variables and described in special comments.
3. **Code Generation**: Run the Python script `generate_plugin.py`, which parses your shader and creates:
   - C++ effect class (`.hpp`, `.cpp`) implementing the main `WallpaperEffect` interface.
   - `CMakeLists.txt` to build the plugin as a shared library (`.so`).
4. **Build**: Add the plugin to the main build system and compile the project.
5. **Installation & Run**: Copy the compiled `.so` file and shaders to the configuration directory, after which the effect becomes available in the application.

## 🛠️ Example: Creating "Simple Wave" Effect

Let's create a simple animated wave effect to demonstrate the entire process.

### Step 1: Create Folder Structure

Inside the `plugins/` directory, create a new folder for your effect. The name should be in `kebab-case`.

```bash
cd plugins
mkdir simple-wave-effect
cd simple-wave-effect
mkdir shaders
```

The structure should look like this:

```
plugins/
├── ico-sphere-effect/
├── pulse-color-effect/
└── simple-wave-effect/   <-- Your new folder
    └── shaders/
```

### Step 2: Write Fragment Shader

Create a file with your shader.
For example, let's create `plugins/simple-wave-effect/shaders/wave_frag.glsl` and add the following code:

```GLSL
// plugins/simple-wave-effect/shaders/wave_frag.glsl
#version 300 es
precision mediump float;

// Input from vertex shader (texture coordinates)
in vec2 v_uv;

// Output color
out vec4 FragColor;

// Standard uniform variables, always available
uniform float time;
uniform vec2 resolution;

// User parameters that will be controlled from config.
// Format: @param <code_name> | <type> | <default_value> | <description>

// @param wave_color | vec3 | 0.1, 0.5, 1.0 | Main wave color (R,G,B)
uniform vec3 wave_color;

// @param speed | float | 2.5 | Wave movement speed
uniform float speed;

// @param frequency | float | 20.0 | Frequency (number) of waves on screen
uniform float frequency;

// @param is_inverted | bool | false | Whether to invert colors
uniform bool is_inverted;

void main() {
    // Normalize coordinates so center is (0,0)
    vec2 uv = v_uv - 0.5;

    // Simple wave equation depending on time and distance from center
    float wave = sin(length(uv) * frequency - time * speed);

    // Make waves smoother
    wave = smoothstep(0.0, 1.0, wave);

    vec3 final_color = wave_color * wave;

    if (is_inverted) {
        final_color = vec3(1.0) - final_color;
    }

    FragColor = vec4(final_color, 1.0);
}
```

**IMPORTANT: Use special `@param` comments for parameters you want to change in `config.json`:**
`// @param <name> | <type> | <default> | <description>`

- **name**: Must exactly match the `uniform` variable name in the shader.
- **type**: Supported types: `float`, `int`, `bool`, `vec3`.
- **default**: Default value. For `vec3` use format `R, G, B`.
- **description**: Text description that will be used in utilities.

You'll also need a **vertex shader**.
Create file `plugins/simple-wave-effect/shaders/wave_vert.glsl`:

```GLSL
// plugins/simple-wave-effect/shaders/wave_vert.glsl
#version 300 es

// Output data for fragment shader
out vec2 v_uv;

// Vertex array for fullscreen triangle
const vec2 positions[3] = vec2[](
    vec2(-1.0, -1.0),
    vec2(3.0, -1.0),
    vec2(-1.0, 3.0)
);

void main() {
    // Select vertex
    vec2 pos = positions[gl_VertexID];

    // Pass UV coordinates to fragment shader
    // (0,0) at bottom left, (1,1) at top right
    v_uv = pos * 0.5 + 0.5;

    // Set vertex position
    gl_Position = vec4(pos, 0.0, 1.0);
}
```

### Step 3: Generate C++ Code

Now that the shaders are ready, return to the project root directory and run the `generate_plugin.py` script. Point it to your plugin folder.

```
# From the project root folder (shader-desk/)
python3 plugins/generate_plugin.py plugins/simple-wave-effect
```

The script will analyze `@param` comments in your `.glsl` file and generate three files inside `plugins/simple-wave-effect/`:

- `simple-wave-effect.hpp` (Class header file)
- `simple-wave-effect.cpp` (Implementation file)
- `CMakeLists.txt` (Build file)

### Step 4: Build and Install Plugin

**Compile the project**:
```
cd build
cmake ..
make 
```

After successful build, the following will appear in `build/effects/` directory:
- `simple-wave-effect.so` — your plugin.
- `shaders/simple-wave-effect/` folder with your shaders.

**"Install" the plugin**: The application looks for plugins in `~/.config/interactive-wallpaper/effects`.

Copy or create a symbolic link to the build results.

### Step 6: Activate and Configure New Effect

1. **Update configuration**: Run `./src/config-manager.sh init` to make it discover your new plugin and add its parameters to `config.json`.
2. **View available effects**:

```
./src/config-manager.sh list
# Output should include:
# - Ico Sphere Effect
# - Pulse Color Effect
# - Simple Wave Effect  <-- Your new effect!
```

3. **Switch to new effect**:
   `./src/config-manager.sh switch "Simple Wave Effect"`

4. **Start/restart wallpaper**:
   `./run.sh restart`

Now you should see blue waves on your desktop! You can adjust their parameters on the fly:

```
# Change color to fiery
./src/config-manager.sh set wave_color '[1.0, 0.2, 0.0]'

# Speed up waves
./src/config-manager.sh set speed 5.0

# Invert colors
./src/config-manager.sh set is_inverted true
```

The application will automatically pick up changes in `config.json` and apply them.

Congratulations! You've created your first plugin. Now you can experiment with more complex shaders and parameters.