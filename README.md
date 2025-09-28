[README ru](README_ru.md) | [README en](README.md)
# Interactive Wayland Wallpaper 🚀

Beautiful, interactive, and easily configurable live wallpapers for your Wayland desktop. The project uses hardware acceleration (OpenGL ES) for rendering dynamic shaders with minimal resource consumption.

The main effect is an animated icosphere that responds to your cursor movements.

## ✨ Features

  * **GPU Acceleration**: Smooth rendering using OpenGL ES, without loading the CPU.
  * **Interactivity**: The wallpaper responds to mouse and touchpad movements.
  * **Flexible Configuration**: All parameters (animation, colors, detail level) are configurable via a simple JSON file.
  * **Hot Reload**: Configuration changes are applied on the fly without restarting the application.
  * **Management Utility**: Comes with a convenient `config-manager.sh` script for managing settings.
  * **Custom Shader Support**: Easily extensible to use other GLSL shaders.

-----

<video src="demonstration.mp4" width="320" height="240" controls></video>

![Example of how the program works](demonstration.mp4)



## 🔧 Installation

The project consists of two components: the wallpapers themselves (`interactive-wallpaper`) and an optional but recommended input handling daemon (`evdev-pointer-daemon`).
They need to be built separately.

### 1. Dependencies

First, make sure you have all the necessary dependencies installed.

**Arch Linux:**

```bash
sudo pacman -S cmake gcc git wayland wayland-protocols libglvnd glm nlohmann-json jq inotify-tools
```

### 2. Building `interactive-wallpaper`

This is the main component responsible for displaying the wallpapers.

```bash
# 1. Clone the wallpaper repository
git clone https://gitea.com/SeeTheWall/shader-desk shader-desk
cd shader-desk

# 2. Create a build directory and build the project
mkdir build
cd build
cmake ..
make -j$(nproc)
```

After a successful build, the executable will be located at `shader-desk/build/interactive-wallpaper`.

### 3. Building `evdev-pointer-daemon` (Optional)

This daemon reads events directly from `/dev/input/` and allows bypassing compositor restrictions on accessing input events for applications without input focus.

If you want `interactive-wallpaper` to respond to mouse and touchpad movements, you need to build and run this daemon.

**Important preliminary steps:**

```bash
# Add the user to the input group to access input devices
sudo usermod -a -G input $USER

# Log out and back in or run the command below to apply group changes
newgrp input
```

**Building the daemon:**

```bash
# 1. Clone the daemon repository
git clone https://gitea.com/SeeTheWall/mouse
cd mouse

# 2. Build the project
mkdir build
cd build
cmake ..
make -j$(nproc)
```

The executable will be located at `mouse/build/evdev-pointer-daemon`.

**Notes:**
- The daemon must run in the background to provide interactivity
- After adding to the `input` group, a reboot may be required
- For Wayland sessions, the daemon provides full access to mouse/touchpad events

-----

## ⚙️ Configuration

### Initializing Configuration

All settings are stored in the file
`~/.config/interactive-wallpaper/config.json`.
To create a default configuration file, use the `config-manager.sh` script:

```bash
# Navigate to the wallpaper directory
cd /path/to/shader-desk

# Run initialization
./src/config-manager.sh init
```

### Managing Settings

**Show current settings:**
```bash
./src/config-manager.sh show
```

**Change the wireframe_mode parameter**
```bash
# Values: true, false, numbers, or arrays in JSON format
./src/config-manager.sh set wireframe_mode false
```

**Change the sphere detail level:**
```bash
./src/config-manager.sh set subdivisions 4
```

**Open the config in a text editor ($EDITOR):**
```bash
./src/config-manager.sh edit
```

All changes are applied automatically thanks to hot reloading\!

-----

## 🚀 Launching and Desktop Integration

The `run.sh` script is intended for convenient launching and automatic startup with the system.

### 1. Configuring `run.sh`

Make the script executable:

```bash
chmod +x run.sh
```

### 2. Autostart in Wayland Compositors

Add a call to `run.sh` in your compositor's configuration file or implement application autostart in another way.

-----

## 🚑 Troubleshooting

**Wallpaper won't start**:

1.  Make sure you have correctly specified the paths in `run.sh`.
2.  Try running `interactive-wallpaper` directly from the terminal (`./build/interactive-wallpaper`) and check the error output.
3.  Make sure your Wayland compositor supports the `wlr-layer-shell` protocol.

**Mouse/touchpad input doesn't work or works strangely**:

1.  Make sure `evdev-pointer-daemon` is running.
2.  The daemon might require permissions to read from `/dev/input/event*`. This can be resolved by adding your user to the `input` group. 
   `sudo usermod -aG input $USER`
   After this, a system reboot is required.

## ❤️ Contributing

Contributions to the project are welcome\! If you have ideas, suggestions, or fixes, please create Issues or Pull Requests.

## 📜 License

This project is distributed under the MIT license. For details, see the `LICENSE` file.