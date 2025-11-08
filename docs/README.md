[README ru](README_ru.md) | [README en](README.md)
# Interactive Wayland Wallpaper

Beautiful, interactive, and easily configurable live wallpapers for your Wayland desktop. The project uses hardware acceleration (OpenGL ES) for rendering dynamic shaders with minimal resource consumption.

The main effect is an animated icosphere that responds to your cursor movements and music.

## ✨ Features

- [x] **GPU Acceleration**: Smooth rendering using OpenGL ES, without loading the CPU
- [x] **Interactivity**: Wallpapers respond to mouse and touchpad movements
- [ ] **Audio Reactivity**: Real-time visualization responds to music
- [x] **Flexible Configuration**: All parameters configurable via JSON configuration
- [x] **Hot Reload**: Changes are applied on the fly without restart
- [x] **Modularity**: Support for plugins with various effects
- [x] **Automatic Management**: Convenient scripts for launch and configuration

## 🎭 Demonstration

[![Play video](../demo/example_green.png)](https://imgur.com/a/shader-CkNPDLc)

[![Play video](../demo/example_red.png)](https://imgur.com/a/shader-CkNPDLc)

[![Play video](../demo/Cube.png)](https://imgur.com/a/shader-CkNPDLc)

[![Play video](../demo/Niri.png)](https://imgur.com/a/shader-CkNPDLc)

## Project Architecture

The project consists of several interacting parts:

#### Main Components:
- **`interactive-wallpaper`**: System core, responsible for displaying wallpapers on the GPU.
- **`evdev-pointer-daemon`** (optional): Daemon for processing mouse/touchpad events bypassing Wayland restrictions.
- **`audio-daemon`** (optional): Daemon for real-time audio analysis and data transmission to the main application.

#### Auxiliary Utilities:
- **`config-manager.sh`**: Script for convenient management of JSON configuration from the command line.
- **`run.sh`**: Script for starting, stopping, and restarting all system components.
- **`plugin-interrogator`**: Utility that allows scripts to get information (name, default parameters) directly from compiled `.so` plugin files.
- **`generate_plugin.py`**: Script for developers that automatically creates C++/CMake "wrapper" for a new plugin based on comments in the GLSL shader.

> The project was tested and built on Arch Linux with the Niri compositor.
> Operation on other distributions is possible but not guaranteed.

## 🏗️ Installation and Setup

### 1. Preparing the Working Directory

Create a common directory for all project components:

```bash
mkdir path/interactive-wallpaper
cd path/interactive-wallpaper
```

### 2. Cloning Repositories

**All three components must be cloned as siblings in the same `path/interactive-wallpaper` directory:**

```bash
# Main application
git clone https://gitea.com/SeeTheWall/shader-desk

# Mouse daemon (for input processing)
git clone https://gitea.com/SeeTheWall/mouse

# Audio analyzer daemon (for sound analysis)
# The audio-daemon has not been published (it is not ready yet), and this part should be skipped.
# git clone https://gitea.com/SeeTheWall/audio-daemon
```

After cloning, the directory structure should look like this:
```
path/interactive-wallpaper/
├── shader-desk/    # Main application
├── mouse/          # Mouse daemon
└── audio-daemon/   # Audio analyzer daemon
```

### 3. Installing Dependencies

Install dependencies only for the components you plan to use.
#### **Step 1: Main Application (Mandatory)**
These commands will install everything necessary to build and run the `interactive-wallpaper` core and its utilities.

* **Arch Linux / Manjaro:**
  ```bash
  sudo pacman -S base-devel cmake git wayland wayland-protocols libglvnd glm nlohmann-json jq inotify-tools
  ```
* **Ubuntu / Debian:**
  ```bash
  sudo apt update && sudo apt install build-essential cmake git wayland-protocols libwayland-dev libglvnd-dev libglm-dev nlohmann-json3-dev jq inotify-tools
  ```
* **Fedora:**
  ```bash
  sudo dnf install cmake gcc-c++ git wayland-devel wayland-protocols-devel mesa-libGL-devel glm-devel nlohmann-json-devel jq inotify-tools
  ```

#### **Step 2: Mouse Daemon (Optional)**
Install only if you need interactivity from a mouse or touchpad.

* **Arch Linux / Manjaro:**
  ```bash
  sudo pacman -S libevdev
  ```
* **Ubuntu / Debian:**
  ```bash
  sudo apt install libevdev-dev
  ```
* **Fedora:**
  ```bash
  sudo dnf install libevdev-devel
  ```

#### **Step 3: Audio Daemon (Optional)**

> This part of the project is not ready at the moment. 
The Audio Daemon has not been published, and this part should be skipped.

Install only if you need audio reactivity.

* **Arch Linux / Manjaro:**
  ```bash
  sudo pacman -S pulseaudio fftw
  ```
* **Ubuntu / Debian:**
  ```bash
  sudo apt install libpulse-dev libfftw3-dev
  ```
* **Fedora:**
  ```bash
  sudo dnf install pulseaudio-libs-devel fftw-devel
  ```

### 4. Building the Main Application

```bash
cd path/interactive-wallpaper/shader-desk
mkdir build && cd build
cmake ..
make -j$(nproc)
```

**What happens during the build:**

- The main `interactive-wallpaper` application is built
- All plugins from the `plugins/` folder are automatically compiled:
- The `build/effects/` folder is formed, containing .so files and a folder with effect shaders.

### Configuring Plugins and Shaders

After successful build, it is necessary to ensure the availability of plugins and shaders for the application:

**Method 1: Copying**
```bash
# Copy plugins and shaders to the configuration directory
cp -r path/interactive-wallpaper/shader-desk/build/effects ~/.config/interactive-wallpaper/
```

**Method 2: Symbolic Link**
```bash
# Create configuration directory if it doesn't exist
mkdir -p ~/.config/interactive-wallpaper

# Create a symbolic link to the built effects
ln -sf path/interactive-wallpaper/shader-desk/build/effects ~/.config/interactive-wallpaper/effects
```

Using a symbolic link is recommended if you plan to experiment with modifications of existing effects and develop your own.

After creating it, there is no need to manually copy the folder after each compilation.

**Installation Check:**
```bash
ls -la ~/.config/interactive-wallpaper/effects/
# Should display:
# ico-sphere-effect.so  pulse-color-effect.so  shaders/
```

After this setup, you can proceed to initialize the configuration using `config-manager.sh init`.

### 5. Building the Mouse Daemon (Optional)

**⚠️ Important: The mouse daemon is needed to bypass the restrictions of Wayland compositors**.

In Wayland, compositors prohibit applications without input focus from receiving mouse events. Our daemon solves this problem by reading events directly from `/dev/input/`.

```bash
# Setting up access rights
sudo usermod -a -G input $USER
# Log out and back in or execute:
newgrp input

# Building the daemon
cd path/interactive-wallpaper/mouse
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### 6. Building the Audio Analyzer Daemon (Optional)

> This part of the project is not ready at the moment. 
The Audio Daemon has not been published, and this part should be skipped.

```bash
cd path/interactive-wallpaper/audio-daemon
mkdir build && cd build
cmake ..
make -j$(nproc)
```

## ⚙️ Configuration Setup

### Initializing Configuration

```bash
cd path/interactive-wallpaper/shader-desk
./src/config-manager.sh init
```

This will create a configuration file in `~/.config/interactive-wallpaper/`.

### Managing Settings

**Help:**
```bash
./src/config-manager.sh help
```

**Viewing Current Settings:**
```bash
./src/config-manager.sh show
```

**List of Available Effects:**
```bash
./src/config-manager.sh list
```

**Changing Parameters:**
```bash
# Enable/disable wireframe mode
./src/config-manager.sh set wireframe_mode false

# Set sphere detail level
./src/config-manager.sh set subdivisions 4

# Configure mouse sensitivity
./src/config-manager.sh set --global mouse_sensitivity 3.0
```

**Manual Configuration Editing:**
```bash
./src/config-manager.sh edit
```

You can also write your own scripts to manage the parameters of your effects.
You can simply write to the file
`~/.config/interactive-wallpaper/config.json`.

## 🚀 System Launch

### Using the run.sh Script

The `run.sh` script automatically manages all system components:

```bash
# Go to the main application directory
cd path/interactive-wallpaper/shader-desk

# Make the script executable
chmod +x run.sh

# Start all components
./run.sh start

# Stop all components  
./run.sh stop

# Restart
./run.sh restart
```

### Manual Component Launch

You can run the binaries manually or develop your own scripts to run them.

**Main Application Only:**
```bash
cd path/interactive-wallpaper/shader-desk/
./build/interactive-wallpaper

cd path/interactive-wallpaper/mouse/
./build/evdev-pointer-daemon --socket /tmp/evdev-pointer.sock

cd path/interactive-wallpaper/audio-daemon/
./build/audio-daemon
```

### Autostart

Add the launch of the shell script
`./path/interactive-wallpaper/shader-desk/run.sh start`
when your compositor loads.

For autostart, you can also create a systemd unit. An example unit is currently missing from the documentation.

## Developing Your Own Effects.

You can create your own effect if you know how to write GLSL shaders and have a basic understanding of C++.

A detailed guide on creating plugins is in the file [PLUGIN_DEV_GUIDE_en.md](PLUGIN_DEV_GUIDE_en.md).

## 🐛 Troubleshooting

**Wallpapers Do Not Start:**
- Make sure the [Wayland compositor supports `wlr-layer-shell`](https://wayland.app/protocols/wlr-layer-shell-unstable-v1#compositor-support) 
- Check the terminal output: `./build/interactive-wallpaper`
- Ensure configuration is created: `./src/config-manager.sh init`
- Ensure you copied the effects to the config folder.

**Mouse Daemon Not Working:**
- Make sure you have completely logged out and back in (logout/login) after adding to the `input` group.
- After logging back in, check that you are a member of the group:
  `groups | grep input`
- Check the socket: `ls -la /tmp/evdev-pointer.sock`

**Audio Not Working:**
- Ensure the audio analyzer daemon is running
- Check audio settings in the configuration
- Ensure there is audio output in the system

**Changes Not Applied:**
- Ensure the application is running with the correct config and there are no typos in the `config.json` file.

## 🤝 Contribution

Contributions to the project are welcome! If you have ideas, suggestions, or fixes, please create Issues or Pull Requests.

## 📜 License

This project is distributed under the MIT license. For details, see the `LICENSE` file.