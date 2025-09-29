#!/bin/bash
# config-manager.sh - configuration manager for interactive-wallpaper

CONFIG_DIR="$HOME/.config/interactive-wallpaper"
CONFIG_FILE="$CONFIG_DIR/config.json"
DEFAULT_CONFIG='{
  "effect": "sphere",
  "interactive": true,
  "sphere_scale": 1.0,
  "wireframe_mode": true,
  "subdivisions": 3,
  "oscill_amp": 0.1,
  "oscill_freq": 2.0,
  "wave_amp": 0.05,
  "wave_freq": 8.0,
  "twist_amp": 0.08,
  "pulse_amp": 0.03,
  "noise_amp": 0.02,
  "background_color": [0.1137, 0.1137, 0.1255],
  "wireframe_color": [0.5, 0.5, 0.7],
  "touchpad_sensitivity": 0.3,
  "mouse_sensitivity": 2.5,
  "constant_rotation_speed": 0.1,
  "rotation_decay": 0.95,
  "min_rotation_speed": 0.001,
  "max_rotation_speed": 5.0
}'

# Create directory and config file if they don't exist
init_config() {
    if [ ! -d "$CONFIG_DIR" ]; then
        mkdir -p "$CONFIG_DIR"
        echo "Configuration directory created: $CONFIG_DIR"
    fi
    
    if [ ! -f "$CONFIG_FILE" ]; then
        echo "$DEFAULT_CONFIG" > "$CONFIG_FILE"
        echo "Default configuration file created: $CONFIG_FILE"
    fi
}

# Read value from configuration
get_config_value() {
    local key="$1"
    jq -r ".$key" "$CONFIG_FILE" 2>/dev/null
}

# Set value in configuration
set_config_value() {
    local key="$1"
    local value="$2"
    
    if ! command -v jq &> /dev/null; then
        echo "Error: jq is not installed. Install with: sudo apt install jq"
        return 1
    fi
    
    jq ".$key = $value" "$CONFIG_FILE" > "$CONFIG_FILE.tmp" && \
    mv "$CONFIG_FILE.tmp" "$CONFIG_FILE"
    echo "Set: $key = $value"
}

# Show current configuration
show_config() {
    if [ -f "$CONFIG_FILE" ]; then
        cat "$CONFIG_FILE" | jq .
    else
        echo "Configuration file not found: $CONFIG_FILE"
    fi
}

# Edit configuration in text editor
edit_config() {
    if [ -z "$EDITOR" ]; then
        EDITOR="nano"
    fi
    
    if [ -f "$CONFIG_FILE" ]; then
        $EDITOR "$CONFIG_FILE"
    else
        echo "Configuration file not found"
    fi
}

# Monitor configuration changes (requires inotify-tools)
monitor_config() {
    if ! command -v inotifywait &> /dev/null; then
        echo "For monitoring, install inotify-tools: sudo apt install inotify-tools"
        return 1
    fi
    
    echo "Monitoring configuration changes... (Ctrl+C to exit)"
    while true; do
        inotifywait -e modify "$CONFIG_FILE" 2>/dev/null
        echo "Configuration changed: $(date)"
        # Here you can add a call to reload configuration in the application
    done
}

# Generate C++ configuration
generate_cpp_config() {
    cat << 'EOF'
// generated_config.hpp - automatically generated configuration file
#pragma once

struct WallpaperConfig {
    const char* effect = "sphere";
    bool wireframe_mode = true;
    int subdivisions = 3; 
    float oscill_amp = 0.1f;
    float oscill_freq = 2.0f;
    float wave_amp = 0.05f;
    float wave_freq = 8.0f;
    float twist_amp = 0.08f;
    float pulse_amp = 0.03f;
    float noise_amp = 0.02f;
    float background_color[3] = {0.1137f, 0.1137f, 0.1255f};
    float wireframe_color[3] = {0.5f, 0.5f, 0.7f};
    bool interactive = true;
};

inline WallpaperConfig load_config() {
    return WallpaperConfig{};
}
EOF
}


# Help
show_help() {
    cat << EOF
Usage: $0 [command]

Commands:
  init      - initialize configuration
  show      - show current configuration
  edit      - edit configuration
  monitor   - monitor configuration changes
  get [key] - get parameter value
  set [key] [value] - set parameter value
  generate  - generate C++ header file
  help      - show this help

Examples:
  $0 init
  $0 set wireframe_mode false
  $0 get effect
  $0 monitor
EOF
}

# Main logic
case "$1" in
    "init")
        init_config
        ;;
    "show")
        show_config
        ;;
    "edit")
        edit_config
        ;;
    "monitor")
        monitor_config
        ;;
    "get")
        if [ -z "$2" ]; then
            echo "Specify parameter to read"
            exit 1
        fi
        get_config_value "$2"
        ;;
    "set")
        if [ -z "$2" ] || [ -z "$3" ]; then
            echo "Specify parameter and value"
            exit 1
        fi
        set_config_value "$2" "$3"
        ;;
    "generate")
        generate_cpp_config
        ;;
    "help"|"-h"|"--help")
        show_help
        ;;
    *)
        echo "Unknown command: $1"
        echo "Use: $0 help for help"
        exit 1
        ;;
esac