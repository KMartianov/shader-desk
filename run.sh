#!/usr/bin/env bash
set -euo pipefail

VERBOSE="${VERBOSE:-}"
log() { 
    if [ -n "${VERBOSE:-}" ]; then
        printf '%s\n' "$*" >&2
    fi
}

# Восстановленные переменные для определения путей
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
WP_DIR="${WP_DIR:-${SCRIPT_DIR}}"
MOUSE_DIR="${MOUSE_DIR:-$(cd "${SCRIPT_DIR}/.." >/dev/null 2>&1 && pwd)/mouse}"
SOCKET_NAME="${SOCKET_NAME:-evdev-pointer.sock}"
WP_BIN="${WP_BIN:-${WP_DIR}/build/interactive-wallpaper}"
MOUSE_BIN="${MOUSE_BIN:-${MOUSE_DIR}/build/evdev-pointer-daemon}"

# default socket computation
default_socket() {
    if [ -n "${XDG_RUNTIME_DIR:-}" ]; then
        echo "${XDG_RUNTIME_DIR}/${SOCKET_NAME}"
    else
        echo "/tmp/evdev-pointer-$(id -u).sock"
    fi
}
DEST_SOCKET="$(default_socket)"

# Функция для завершения процессов по пути к бинарнику
kill_existing() {
    local bin="$1"
    local bin_name="$(basename "$bin")"
    
    log "Looking for existing processes: $bin_name"
    
    # Ищем PID процессов с таким же исполняемым файлом
    if command -v pgrep >/dev/null 2>&1; then
        # Используем pgrep для поиска по имени
        for pid in $(pgrep -x "$bin_name" 2>/dev/null || true); do
            if [ -r "/proc/$pid/exe" ]; then
                local exe="$(readlink -f "/proc/$pid/exe" 2>/dev/null || true)"
                if [ "$exe" = "$(readlink -f "$bin")" ]; then
                    log "Killing process $pid: $bin_name"
                    kill "$pid" 2>/dev/null || true
                fi
            fi
        done
    fi
    
    # Дополнительная проверка через ps
    for pid in $(ps aux | grep "$(readlink -f "$bin")" | grep -v grep | awk '{print $2}' 2>/dev/null || true); do
        log "Killing process $pid: $bin_name"
        kill "$pid" 2>/dev/null || true
        sleep 0.1
    done
    
    # Принудительное завершение если процессы еще остались
    for pid in $(pgrep -x "$bin_name" 2>/dev/null || true); do
        if [ -r "/proc/$pid/exe" ]; then
            local exe="$(readlink -f "/proc/$pid/exe" 2>/dev/null || true)"
            if [ "$exe" = "$(readlink -f "$bin")" ]; then
                log "Force killing process $pid: $bin_name"
                kill -9 "$pid" 2>/dev/null || true
            fi
        fi
    done
}

# Быстрый detached запуск
start_detached() {
    local bin="$1"; shift
    local args=("$@")
    local bin_dir
    bin_dir="$(dirname "$bin")"
    log "Starting: $bin ${args[*]}"
    ( cd "$bin_dir" 2>/dev/null || true; setsid "$bin" "${args[@]}" </dev/null >/dev/null 2>&1 & ) || true
}

# Основная логика: убить старые процессы и запустить новые
restart_process() {
    local bin="$1"; shift
    local args=("$@")
    
    # Проверяем существование бинарника
    if [ ! -f "$bin" ]; then
        log "Binary not found: $bin"
        return 1
    fi
    
    # Завершаем существующие процессы
    kill_existing "$bin"
    
    # Небольшая пауза перед запуском нового процесса
    sleep 0.2
    
    # Запускаем новый процесс
    start_detached "$bin" "${args[@]}"
}

# Перезапускаем оба процесса
restart_process "${WP_BIN}"
restart_process "${MOUSE_BIN}" --socket "${DEST_SOCKET}"

log "All processes restarted"

exit 0