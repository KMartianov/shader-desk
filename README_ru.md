# Shader Desk

Shader Desk — модульный, высокопроизводительный движок интерактивных обоев для Wayland. Проект спроектирован на базе архитектуры микроядра: рендеринг, сбор данных и визуальные эффекты строго изолированы. Тяжёлые операции (например, быстрое преобразование Фурье для звука или чтение `/dev/input`) вынесены в самостоятельные процессы-демоны. Они передают данные в графическое ядро через неблокирующие UNIX-сокеты (`epoll`, `MSG_DONTWAIT`), что гарантирует нулевую задержку (zero-latency) и защищает графический сеанс пользователя от зависаний.

---

## Оглавление документации

**Для пользователей**
* [Установка и настройка](docs/ru/01-installation-and-setup.md) — AUR, Nix, ручная сборка, права доступа, автозагрузка.
* [Конфигурация и сцены](docs/ru/02-configuration-and-scenes.md) — Lua-управление, теги, слои, фильтры, пресеты.
* [Рабочее пространство и шейдеры](docs/ru/03-workspaces-and-shaders.md) — песочница, Hot-Reload, ShaderToy, доступ к системным данным.
* [Утилиты командной строки](docs/ru/08-utilities-reference.md) — `interactive-wallpaper`, `shader-desk-ctl`, `shader-desk-run`, генератор плагинов.

**Для разработчиков**
* [SDK: визуальные плагины](docs/ru/04-sdk-visual-plugins.md) — C++-классы `WallpaperEffect` и `KinematicEffect`, стандартный конвейер, вложенные фильтры.
* [SDK: провайдеры данных](docs/ru/05-sdk-data-providers.md) — конвейер данных, демоны, сокеты, BlackBoard.
* [Lua API справочник](docs/ru/06-lua-api-reference.md) — полное описание `core` и `ctl`.
* [Обзор архитектуры](docs/ru/07-architecture-overview.md) — микроядро, epoll, C-ABI, Shadow-Commit, Ping-Pong FBO.
* [SDF Hub Concept](docs/ru/sdf_hub_concept.md) — концепция модульного реймарчинга для продвинутых разработчиков.

---

## Совместимость с графическими окружениями

Shader Desk использует протокол **`zwlr_layer_shell_v1`** для вывода изображения на фон рабочего стола. Работает исключительно в сеансах Wayland (X11 не поддерживается).

**Полная поддержка:**
* Hyprland
* Sway
* KWin (KDE Plasma Wayland)
* COSMIC
* niri, river, Labwc, Wayfire, GameScope и другие композиторы на базе `wlroots`.

**Не поддерживается:**
* GNOME (Mutter) и Cinnamon (Muffin) — эти окружения не реализуют `wlr-layer-shell`, поэтому вывод обоев на фон рабочего стола в них невозможен.

---

## Быстрый старт

### Установка из AUR (рекомендуется для Arch Linux)

```bash
yay -S shader-desk-git
```

### Установка через Nix

```bash
nix profile install github:KMartianov/shader-desk
```

### Ручная сборка

```bash
git clone https://github.com/KMartianov/shader-desk.git
cd shader-desk
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
sudo cmake --install build
```

Подробные инструкции по установке и настройке прав доступа см. в разделе **[Установка и настройка](docs/ru/01-installation-and-setup.md)**.

---

## Инициализация конфигурации

После установки выполните:

```bash
interactive-wallpaper --init-workspace
interactive-wallpaper --init-config
```

Эти команды создадут пользовательскую песочницу в `~/.config/interactive-wallpaper/` и сгенерируют файлы настройки для всех доступных плагинов. После этого можно редактировать GLSL-шейдеры и Lua-сцены — движок будет отслеживать изменения и перезагружать их на лету.

---

## Управление работающим движком

Через утилиту `shader-desk-ctl` можно динамически изменять параметры обоев, переключать пресеты и управлять провайдерами данных.

```bash
# Изменить параметр для всех мониторов
shader-desk-ctl "ctl.set('wireframe_mode', false, '*')"

# Переключить булев параметр на конкретном мониторе
shader-desk-ctl "ctl.toggle('enable_stripes', 'eDP-1')"

# Применить пресет
shader-desk-ctl "ctl.preset('Icosahedron Sphere', 'cyberpunk', 'DP-1')"

# Получить статус в JSON
shader-desk-ctl "ctl.status()"
```

Полный список команд доступен в [справочнике Lua API](docs/ru/06-lua-api-reference.md#2-модуль-ctl-внешнее-управление) и в [руководстве по утилите](docs/ru/08-utilities-reference.md#2-управление-через-сокет-shader-desk-ctl).

---

## План развития

Проект находится в стадии активной разработки.

**Реализовано:**
* Событийный цикл с нулевой задержкой на базе `epoll`.
* Hot-Reload система (Lua + GLSL) через `inotify` с Shadow-Commit.
* Демоны аудио (PipeWire/PulseAudio + FFTW) и указателя (libevdev).
* Динамический загрузчик C-ABI плагинов с Fallback-приоритетами.
* Автоматическая генерация гибридных Lua-конфигураций на основе C++ метаданных.
* Адаптер для запуска шейдеров с ShaderToy «из коробки».
* Интеграция с Tracy Profiler (Zero-Allocation в главном цикле).

**Запланировано:**
* Vulkan Backend.
* Новые провайдеры данных (MPRIS, погода, системные метрики).
* Среда визуального программирования шейдеров.

---

## Лицензия

Исходный код распространяется под лицензией [MIT](LICENSE). Разрешается свободное использование, модификация и интеграция в сторонние проекты.
