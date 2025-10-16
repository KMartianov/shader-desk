#!/bin/bash
# config-manager.sh - configuration manager for interactive-wallpaper

CONFIG_DIR="$HOME/.config/interactive-wallpaper"
EFFECTS_DIR="$CONFIG_DIR/effects"
CONFIG_FILE="$CONFIG_DIR/config.json"

# Helper function to check for jq
check_jq() {
    if ! command -v jq &> /dev/null; then
        echo "Error: jq is not installed. It is required for this script." >&2
        echo "Please install it with: sudo apt install jq" >&2
        exit 1
    fi
}

# Converts a project name like 'ico-sphere-effect' to a pretty name 'Ico Sphere Effect'
project_to_pretty_name() {
    local project_name="$1"
    # Replace hyphens with spaces and capitalize words
    echo "$project_name" | sed -e 's/-/ /g' -e 's/\b\(.\)/\u\1/g'
}

# Discovers available plugins by looking for .so files
discover_plugins() {
    if [ ! -d "$EFFECTS_DIR" ]; then
        return
    fi
    for so_file in "$EFFECTS_DIR"/*.so; do
        if [ -f "$so_file" ]; then
            # Get filename without extension
            basename "$so_file" .so
        fi
    done
}

# Queries a plugin .so file to extract default parameters as a JSON string
get_plugin_params_json() {
    local plugin_name="$1"
    local so_file="$EFFECTS_DIR/${plugin_name}.so"
    
    # Путь к нашей новой утилите. Предполагаем, что скрипт запускается из корня проекта.
    local interrogator_bin="./build/plugin-interrogator"

    if [ ! -f "$so_file" ]; then
        echo "Warning: Plugin library not found for '$plugin_name' at '$so_file'." >&2
        echo "{}"
        return
    fi
    
    if [ ! -x "$interrogator_bin" ]; then
        echo "Error: plugin-interrogator tool not found or not executable at '$interrogator_bin'." >&2
        echo "Please build the project first." >&2
        echo "{}"
        return
    fi

    # Вызываем утилиту и возвращаем её JSON-вывод
    "$interrogator_bin" "$so_file"
}


# Create directory and a comprehensive config file
init_config() {
    check_jq

    if [ ! -d "$EFFECTS_DIR" ]; then
        mkdir -p "$EFFECTS_DIR"
        echo "Configuration directory created: $CONFIG_DIR"
        echo "Effects directory created: $EFFECTS_DIR"
        echo "NOTE: You need to copy your compiled plugin (.so) files and their shaders to this directory."
    fi
    
    echo "Scanning for plugins..."
    local plugins
    plugins=$(discover_plugins)
    
    if [ -z "$plugins" ]; then
        echo "Warning: No plugins (.so files) found in $EFFECTS_DIR."
        # ... (код для создания placeholder-конфига остается тот же) ...
        jq -n '{
          "effect_name": "",
          "interactive": true,
          "mouse_sensitivity": 2.5,
          "touchpad_sensitivity": 0.3,
          "effect_settings": {},
          "effects_defaults": {}
        }' > "$CONFIG_FILE"
        echo "Placeholder configuration file created: $CONFIG_FILE"
        return
    fi

    local default_effect_name=""
    local defaults_json="{}"
    
    for plugin_filename in $plugins; do
        echo " - Querying plugin: $plugin_filename.so..."
        
        # Получаем всю информацию (имя и параметры) от утилиты
        local plugin_info_json
        plugin_info_json=$(get_plugin_params_json "$plugin_filename")

        # Извлекаем каноническое имя и параметры с помощью jq
        local canonical_name
        canonical_name=$(echo "$plugin_info_json" | jq -r '.name')
        local params_json
        params_json=$(echo "$plugin_info_json" | jq '.defaults')

        if [ -z "$canonical_name" ] || [ "$canonical_name" == "null" ]; then
            echo "   Warning: Could not retrieve canonical name for $plugin_filename. Skipping."
            continue
        fi

        echo "   -> Found plugin with canonical name: '$canonical_name'"

        # Устанавливаем первый найденный плагин как эффект по умолчанию
        if [ -z "$default_effect_name" ]; then
            default_effect_name="$canonical_name"
        fi
        
        # Добавляем параметры в общий список defaults
        defaults_json=$(echo "$defaults_json" | jq --arg key "$canonical_name" --argjson val "$params_json" '. + {($key): $val}')
    done

    local default_settings
    default_settings=$(echo "$defaults_json" | jq --arg key "$default_effect_name" '.[$key]')

    # Собираем финальный config.json, используя канонические имена
    jq -n \
      --arg effect_name "$default_effect_name" \
      --argjson settings "$default_settings" \
      --argjson defaults "$defaults_json" \
      '{
        "effect_name": $effect_name,
        "interactive": true,
        "mouse_sensitivity": 2.5,
        "touchpad_sensitivity": 0.3
      } + {"effect_settings": $settings} + {"effects_defaults": $defaults}' > "$CONFIG_FILE"

    echo "Default configuration file created: $CONFIG_FILE"
    echo "Default effect set to: $default_effect_name"
}

# Read value from configuration
get_config_value() {
    local key="$1"
    # First check in effect_settings, then in the root for global settings
    local value
    value=$(jq -r ".effect_settings.\"$key\" // .\"$key\"" "$CONFIG_FILE" 2>/dev/null)
    
    if [[ "$value" != "null" ]]; then
        echo "$value"
    else
        echo "Error: Key '$key' not found in configuration." >&2
        return 1
    fi
}

# Set value in configuration
set_config_value() {
    local key="$1"
    local value="$2"
    local scope="effect_settings"

    if [[ "$1" == "--global" ]]; then
        scope="root"
        key="$2"
        value="$3"
    fi

    if [[ "$scope" == "root" ]]; then
        jq ".$key = $value" "$CONFIG_FILE" > "$CONFIG_FILE.tmp" && \
        mv "$CONFIG_FILE.tmp" "$CONFIG_FILE"
        echo "Set global: $key = $value"
    else
        jq ".effect_settings.\"$key\" = $value" "$CONFIG_FILE" > "$CONFIG_FILE.tmp" && \
        mv "$CONFIG_FILE.tmp" "$CONFIG_FILE"
        echo "Set effect parameter: $key = $value"
    fi
}

# Discovers and lists available effects by directly querying the plugin files
list_effects() {
    echo "Available effects:"
    
    local plugin_files
    plugin_files=$(discover_plugins) # Gets basenames like 'ico-sphere-effect'

    if [ -z "$plugin_files" ]; then
        echo "  No plugins (.so files) found in $EFFECTS_DIR"
        return
    fi

    local interrogator_bin="./build/plugin-interrogator"
    if [ ! -x "$interrogator_bin" ]; then
        echo "Error: plugin-interrogator tool not found or not executable at '$interrogator_bin'." >&2
        echo "Please build the project first." >&2
        return 1
    fi

    for plugin_filename in $plugin_files; do
        local so_file="$EFFECTS_DIR/${plugin_filename}.so"
        
        if [ -f "$so_file" ]; then
            # Query the plugin file directly and parse its JSON output to get the name
            local canonical_name
            canonical_name=$("$interrogator_bin" "$so_file" | jq -r '.name')
            
            if [ -n "$canonical_name" ] && [ "$canonical_name" != "null" ]; then
                echo "  - $canonical_name"
            else
                echo "  - [Warning] Could not read name from: ${plugin_filename}.so"
            fi
        fi
    done
}

# Switch the active effect and load its default settings
switch_effect() {
    local pretty_name="$1"
    
    if ! jq -e ".effects_defaults | has(\"$pretty_name\")" "$CONFIG_FILE" > /dev/null; then
        echo "Error: Effect '$pretty_name' not found in configuration defaults." >&2
        echo "Run '$0 list' to see available effects." >&2
        return 1
    fi
    
    # Get the defaults for the new effect
    local new_settings
    new_settings=$(jq ".effects_defaults.\"$pretty_name\"" "$CONFIG_FILE")
    
    # Update the config file
    jq \
      --arg name "$pretty_name" \
      --argjson settings "$new_settings" \
      '(.effect_name = $name) | (.effect_settings = $settings)' \
      "$CONFIG_FILE" > "$CONFIG_FILE.tmp" && \
    mv "$CONFIG_FILE.tmp" "$CONFIG_FILE"
    
    echo "Switched active effect to: $pretty_name"
    echo "Effect settings have been reset to default."
}


# Show current configuration
show_config() {
    check_jq
    if [ -f "$CONFIG_FILE" ]; then
        jq . "$CONFIG_FILE"
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
        inotifywait -q -e modify "$CONFIG_FILE"
        echo "Configuration changed: $(date). (The application should reload it automatically)"
    done
}

# Help
show_help() {
    cat << EOF
Usage: $0 [command]

Enhanced configuration manager for interactive-wallpaper with plugin support.

Commands:
  init          - Initialize config by scanning for plugins and their parameters.
  show          - Show the current configuration in JSON format.
  edit          - Open the configuration file in a text editor.
  monitor       - Monitor the configuration file for changes.
  
  list          - List all available effects (plugins).
  switch [name] - Switch the active effect to '[name]' and load its defaults.
                  Use quotes for names with spaces, e.g., '$0 switch "Pulse Color Effect"'.
  
  get [key]     - Get a value. Checks effect parameters first, then global settings.
  set [key] [val] - Set an effect parameter's value.
  set --global [key] [val] - Set a global configuration value.

Examples:
  $0 init
  $0 list
  $0 switch "Pulse Color Effect"
  $0 set pulse_speed 2.0
  $0 set --global interactive false
  $0 get pulse_speed
  $0 get interactive
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
    "list")
        list_effects
        ;;
    "switch")
        if [ -z "$2" ]; then
            echo "Error: Specify the effect name to switch to." >&2
            exit 1
        fi
        switch_effect "$2"
        ;;
    "get")
        if [ -z "$2" ]; then
            echo "Error: Specify parameter to read." >&2
            exit 1
        fi
        get_config_value "$2"
        ;;
    "set")
        if [[ "$2" == "--global" ]]; then
            if [ -z "$3" ] || [ -z "$4" ]; then
                echo "Error: Specify global parameter and value." >&2
                exit 1
            fi
            set_config_value --global "$3" "$4"
        else
            if [ -z "$2" ] || [ -z "$3" ]; then
                echo "Error: Specify parameter and value." >&2
                exit 1
            fi
            set_config_value "$2" "$3"
        fi
        ;;
    "help"|"-h"|"--help")
        show_help
        ;;
    "")
        echo "No command specified." >&2
        show_help
        exit 1
        ;;
    *)
        echo "Unknown command: $1"
        echo "Use '$0 help' for a list of commands."
        exit 1
        ;;
esac