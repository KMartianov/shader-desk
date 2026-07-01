#!/usr/bin/env bash
set -euo pipefail

# --- Конфигурация путей ---
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="${PROJECT_ROOT:-$SCRIPT_DIR}"

WP_BIN="${PROJECT_ROOT}/build/interactive-wallpaper"

MOUSE_DAEMON_DIR="${PROJECT_ROOT}/../mouse"
MOUSE_BIN="${MOUSE_DAEMON_DIR}/build/evdev-pointer-daemon"

# НОВОЕ: Добавляем пути к аудио-демону
AUDIO_DAEMON_DIR="${PROJECT_ROOT}/../audio-daemon"
AUDIO_BIN="${AUDIO_DAEMON_DIR}/build/audio-daemon"

DEST_SOCKET="${XDG_RUNTIME_DIR:-/tmp}/evdev-pointer.sock"
LOCK_DIR="${XDG_RUNTIME_DIR:-/tmp}/interactive-wallpaper.lock"

if ! mkdir "$LOCK_DIR" 2>/dev/null; then
    echo "Script is already running. Exiting." >&2
    exit 1
fi
trap 'rm -rf "$LOCK_DIR"' EXIT

# --- Функции ---
is_running() {
    local bin_path="$1"
    pgrep -f "^$bin_path" > /dev/null
}

kill_and_wait() {
    local bin_path="$1"
    local bin_name="$(basename "$bin_path")"

    if ! is_running "$bin_path"; then return 0; fi

    echo "Stopping all instances of $bin_name..."
    pkill -f "^$bin_path" || true

    for _ in {1..10}; do
        if ! is_running "$bin_path"; then return 0; fi
        sleep 0.1
    done
    pkill -9 -f "^$bin_path" || true
}

start_detached() {
    local bin_path="$1"; shift
    local bin_name="$(basename "$bin_path")"
    local log_path="${XDG_RUNTIME_DIR:-/tmp}/${bin_name}.log"
    local args=("$@")

    if [ ! -x "$bin_path" ]; then
        echo "Error: File not found or not executable: $bin_path" >&2
        return 1
    fi

    echo "Starting ${bin_name}... Log: ${log_path}"
    setsid "$bin_path" "${args[@]}" >"$log_path" 2>&1 &
}

# --- Логика управления ---
do_start() {
    echo "Executing start..."

    # 1. Запуск демона мыши
    if [ -x "$MOUSE_BIN" ] && ! is_running "$MOUSE_BIN"; then
        start_detached "$MOUSE_BIN" --socket "${DEST_SOCKET}" || true
    fi

    # 2. НОВОЕ: Запуск Аудио демона
    if [ -x "$AUDIO_BIN" ]; then
        if is_running "$AUDIO_BIN"; then
            echo "Audio daemon is already running."
        else
            if ! start_detached "$AUDIO_BIN"; then
                echo "Warning: failed to start audio daemon."
            fi
        fi
    else
        echo "Notice: Audio daemon not found at: $AUDIO_BIN"
    fi

    # 3. Основное приложение
    if ! is_running "$WP_BIN"; then
        start_detached "$WP_BIN"
    fi
    echo "Start command finished."
}

do_stop() {
    echo "Executing stop..."
    kill_and_wait "$WP_BIN"
    kill_and_wait "$AUDIO_BIN" # НОВОЕ: Останавливаем аудио
    kill_and_wait "$MOUSE_BIN"
    echo "All services have been stopped."
}

ACTION="${1:-start}"

case "$ACTION" in
    start) do_start ;;
    stop) do_stop ;;
    restart)
        do_stop
        sleep 0.2
        do_start
        ;;
    *)
        echo "Usage: $0 {start|stop|restart}" >&2
        exit 1
        ;;
esac