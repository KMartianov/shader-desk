# Установка и базовая настройка

Shader Desk — интерактивный движок обоев для Wayland, использующий OpenGL ES 3.0 и GLSL-шейдеры. В этом руководстве описаны все способы установки, настройка прав доступа и автозагрузка.

---

## 1. Установка из AUR (рекомендуемый способ для Arch Linux)

Для пользователей Arch Linux и его производных (Manjaro, EndeavourOS, Garuda) проект доступен в AUR как пакет `shader-desk-git`. Он автоматически собирается из актуального состояния репозитория и включает все необходимые зависимости.

**Установка через AUR-помощник (рекомендуется):**

```bash
yay -S shader-desk-git
```
или
```bash
paru -S shader-desk-git
```

**Ручная установка (без помощника):**

```bash
git clone https://aur.archlinux.org/shader-desk-git.git
cd shader-desk-git
makepkg -si
```

Пакет устанавливает все компоненты: графическое ядро (`interactive-wallpaper`), демоны (`audio-daemon`, `evdev-pointer-daemon`), скрипт-обёртку (`shader-desk-run`), плагины и системные конфигурации. После установки переходите к разделу [Настройка прав доступа](#4-настройка-прав-доступа).

---

## 2. Установка через Nix

Для пользователей NixOS или тех, кто использует менеджер пакетов Nix в других дистрибутивах, в репозитории присутствует файл `flake.nix`.

**Установка в профиль пользователя (из удалённого репозитория):**

```bash
nix profile install github:KMartianov/shader-desk
```

**Локальная сборка из клонированного репозитория:**

```bash
git clone https://github.com/KMartianov/shader-desk.git
cd shader-desk
nix build .#default
nix profile install ./result
```

**Запуск без установки (временная оболочка):**

```bash
nix shell github:KMartianov/shader-desk -c shader-desk-run
```

Файл `flake.nix` также настраивает обёртку (`wrapProgram`) для корректного поиска динамически загружаемых библиотек OpenGL и драйверов на NixOS.

---

## 3. Ручная сборка из исходного кода (для разработчиков)

Если вы планируете модифицировать движок, разрабатывать собственные плагины или просто хотите собрать проект без пакетного менеджера, используйте ручную сборку через CMake.

### 3.1. Установка зависимостей

В зависимости от вашего дистрибутива установите следующие пакеты:

**Arch Linux / Manjaro:**
```bash
sudo pacman -S base-devel cmake pkgconf wayland wayland-protocols luajit fftw libpulse libevdev
```

**Debian / Ubuntu:**
```bash
sudo apt update
sudo apt install build-essential cmake pkg-config libwayland-dev wayland-protocols libegl1-mesa-dev libgles2-mesa-dev libluajit-5.1-dev libfftw3-dev libpulse-dev libevdev-dev
```

**Fedora:**
```bash
sudo dnf install gcc-c++ cmake pkgconf wayland-devel wayland-protocols-devel mesa-libEGL-devel mesa-libGLES-devel luajit-devel fftw-devel pulseaudio-libs-devel libevdev-devel
```

### 3.2. Клонирование и сборка

```bash
git clone https://github.com/KMartianov/shader-desk.git
cd shader-desk
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

**Дополнительные флаги сборки (опционально):**

- `-DBUILD_AUDIO_DAEMON=OFF` — отключает сборку демона анализа спектра (если не нужна аудиореактивность).
- `-DBUILD_EVDEV_DAEMON=OFF` — отключает сборку демона захвата устройств ввода (если не нужна реакция на мышь).
- `-DENABLE_PROFILING=ON` — включает интеграцию с Tracy Profiler (только для разработчиков).

### 3.3. Установка в систему (FHS)

```bash
sudo cmake --install build
```

Файлы будут распределены по стандартным путям:
- `/usr/bin/interactive-wallpaper` — графическое ядро.
- `/usr/bin/shader-desk-run` — скрипт-обёртка.
- `/usr/lib/shader-desk/plugins/` — скомпилированные плагины.
- `/usr/share/shader-desk/` — системные шейдеры и Lua-конфигурации.

**Портативный режим:** Собранный бинарный файл можно запускать напрямую из `./build/interactive-wallpaper` без установки.

---

## 4. Настройка прав доступа

Для работы демона `evdev-pointer-daemon`, который считывает данные напрямую из устройств ввода Linux (`/dev/input/event*`), ваш пользователь должен быть в группе `input`. Без этого мышь и тачпад не будут взаимодействовать с обоями.

Добавьте пользователя в группу `input`:
```bash
sudo usermod -aG input $USER
```

**Важно:** После выполнения команды необходимо полностью выйти из системы (Log out) и войти заново, либо перезагрузить компьютер, чтобы изменения вступили в силу.

---

## 5. Автозагрузка

### Вариант А: Через Systemd (рекомендуется)

Проект поставляется с файлом пользовательского сервиса `shader-desk.service`. Чтобы он работал корректно, оконный менеджер должен пробросить переменную окружения `WAYLAND_DISPLAY` в сеанс systemd.

**Для Hyprland** (`~/.config/hypr/hyprland.conf`):
```text
exec-once = systemctl --user import-environment WAYLAND_DISPLAY XDG_CURRENT_DESKTOP
```

**Для Sway** (`~/.config/sway/config`):
```text
exec systemctl --user import-environment WAYLAND_DISPLAY XDG_CURRENT_DESKTOP
```

Затем включите и запустите сервис:
```bash
systemctl --user enable --now shader-desk.service
```

### Вариант Б: Запуск напрямую из оконного менеджера

Если вы не используете systemd для управления пользовательскими сессиями, добавьте в конфигурацию композитора вызов скрипта-обёртки:

**Hyprland:**
```text
exec-once = shader-desk-run
```

**Sway:**
```text
exec shader-desk-run
```

Скрипт `shader-desk-run` корректно запускает демоны и ядро, а при завершении безопасно останавливает все дочерние процессы.

---

## 6. Следующие шаги

Движок успешно установлен и запущен. По умолчанию отображается базовая сцена, загруженная из системной директории.

Для персонализации — привязки разных эффектов к мониторам, настройки аудиовизуализации, создания собственных сцен — перейдите к следующему разделу:  
**[Конфигурация и сцены](02-configuration-and-scenes.md)**.
