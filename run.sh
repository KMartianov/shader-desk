#!/usr/bin/env bash
set -euo pipefail

# --- Конфигурация путей ---
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# Предполагаем, что скрипт лежит в корне проекта
PROJECT_ROOT="${PROJECT_ROOT:-$SCRIPT_DIR}"

# Пути к исполняемым файлам
WP_BIN="${PROJECT_ROOT}/build/interactive-wallpaper"
# Предположим, что демон мыши - это отдельный проект в той же директории
MOUSE_DAEMON_DIR="${PROJECT_ROOT}/../mouse" # Пример
MOUSE_BIN="${MOUSE_DAEMON_DIR}/build/evdev-pointer-daemon"

# --- Настройка сокета ---
SOCKET_NAME="evdev-pointer.sock"
DEST_SOCKET="${XDG_RUNTIME_DIR:-/tmp}/${SOCKET_NAME}"

# --- Механизм блокировки (Locking) ---
# Путь к lock-файлу для предотвращения двойного запуска
LOCK_DIR="${XDG_RUNTIME_DIR:-/tmp}/interactive-wallpaper.lock"

# Пытаемся создать lock-директорию. Если не получается - другой скрипт уже работает.
if ! mkdir "$LOCK_DIR" 2>/dev/null; then
    echo "Script is already running. Exiting." >&2
    exit 1
fi
# Гарантируем удаление lock-директории при выходе из скрипта
trap 'rm -rf "$LOCK_DIR"' EXIT

# --- Функции ---

# Проверяет, запущен ли процесс по его имени в командной строке.
is_running() {
    local bin_path="$1"
    pgrep -f "$(basename "$bin_path")" > /dev/null
}

# Завершает все экземпляры процесса по имени и ждет их остановки.
kill_and_wait() {
    local bin_path="$1"
    local bin_name
    bin_name="$(basename "$bin_path")"

    if ! is_running "$bin_path"; then
        return 0
    fi

    echo "Stopping all instances of $bin_name..."
    pkill -f "$bin_name" || true

    for _ in {1..10}; do
        if ! is_running "$bin_path"; then
            echo "All processes of $bin_name stopped."
            return 0
        fi
        sleep 0.1
    done

    echo "Some processes of $bin_name did not respond, sending kill -9..."
    pkill -9 -f "$bin_name" || true
}

# Запускает процесс в фоне с логированием.
start_detached() {
    local bin_path="$1"; shift
    local bin_name
    bin_name="$(basename "$bin_path")"
    local log_path="${XDG_RUNTIME_DIR:-/tmp}/${bin_name}.log"
    local args=("$@")

    if [ ! -x "$bin_path" ]; then
        echo "Error: File not found or not executable: $bin_path" >&2
        return 1
    fi

    echo "Starting ${bin_name}... Log file: ${log_path}"
    setsid "$bin_path" "${args[@]}" >"$log_path" 2>&1 &
}

# --- Логика управления ---

do_start() {
    echo "Executing start..."

    # 1. Демон мыши (необязательный)
    if [ -x "$MOUSE_BIN" ]; then
        if is_running "$MOUSE_BIN"; then
            echo "Mouse daemon is already running."
        else
            # Если запуск неудачен, это не должно остановить скрипт
            if ! start_detached "$MOUSE_BIN" --socket "${DEST_SOCKET}"; then
                echo "Warning: failed to start mouse daemon (non-fatal). Some functionality may be unavailable."
            fi
        fi
    else
        echo "Notice: Mouse daemon not found or not executable at: $MOUSE_BIN"
        echo "        Skipping start of mouse daemon. Some functionality may be unavailable."
    fi

    # 2. Основное приложение (обязательное)
    if is_running "$WP_BIN"; then
        echo "Interactive wallpaper is already running."
    else
        start_detached "$WP_BIN"
    fi
    echo "Start command finished."
}

do_stop() {
    echo "Executing stop..."
    # Останавливаем в обратном порядке
    kill_and_wait "$WP_BIN"
    # Попытка остановить демон мыши — даже если бинарник отсутствует, поиск по имени корректно завершится
    kill_and_wait "$MOUSE_BIN"
    echo "All services have been stopped."
}

# --- Точка входа ---
ACTION="${1:-start}"

case "$ACTION" in
    start)
        do_start
        ;;
    stop)
        do_stop
        ;;
    restart)
        echo "Executing restart..."
        do_stop
        sleep 0.2
        do_start
        ;;
    *)
        echo "Usage: $0 {start|stop|restart}" >&2
        exit 1
        ;;
esac
