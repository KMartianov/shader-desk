#!/usr/bin/env bash
# Wrapper for safe startup of Shader Desk and its daemons.

# Function to clean up child processes (daemons) on exit
cleanup() {
    # Kill all background processes started by this script
    kill $(jobs -p) 2>/dev/null
    wait $(jobs -p) 2>/dev/null
}
# Trap termination signals (Ctrl+C, kill, WM close, systemd stop)
trap cleanup SIGINT SIGTERM EXIT

# 1. Path resolution (Portable mode vs System installation)
SCRIPT_DIR=$(dirname "$(realpath "$0")")

# Look for the core (in the same folder, build-release, or system $PATH)
if [ -x "$SCRIPT_DIR/interactive-wallpaper" ]; then
    CORE_BIN="$SCRIPT_DIR/interactive-wallpaper"
elif [ -x "$SCRIPT_DIR/../build-release/interactive-wallpaper" ]; then
    CORE_BIN="$SCRIPT_DIR/../build-release/interactive-wallpaper"
elif command -v interactive-wallpaper >/dev/null 2>&1; then
    CORE_BIN="interactive-wallpaper"
else
    echo "[Error] interactive-wallpaper binary not found!"
    exit 1
fi

# Look for daemons (they may be missing, which is normal)
AUDIO_BIN=""
EVDEV_BIN=""

# Function to find an executable binary
find_daemon() {
    local name=$1
    if [ -x "$SCRIPT_DIR/$name" ]; then echo "$SCRIPT_DIR/$name"
    elif [ -x "$SCRIPT_DIR/../build-release/$name" ]; then echo "$SCRIPT_DIR/../build-release/$name"
    elif command -v "$name" >/dev/null 2>&1; then echo "$name"
    fi
}

AUDIO_BIN=$(find_daemon "audio-daemon")
EVDEV_BIN=$(find_daemon "evdev-pointer-daemon")

# 2. Safely start daemons in the background
if [ -n "$AUDIO_BIN" ]; then
    echo "[Launcher] Starting Audio Daemon..."
    "$AUDIO_BIN" &
else
    echo "[Launcher] Audio Daemon not found. Audio-reactive features will be disabled."
fi

if [ -n "$EVDEV_BIN" ]; then
    echo "[Launcher] Starting Evdev Pointer Daemon..."
    "$EVDEV_BIN" &
else
    echo "[Launcher] Evdev Pointer Daemon not found. Mouse interactivity will be disabled."
fi

# 3. Start the graphics core (script blocks here until the core exits)
echo "[Launcher] Starting Core Engine..."
"$CORE_BIN" "$@"