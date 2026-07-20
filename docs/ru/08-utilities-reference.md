# Утилиты командной строки

Shader Desk включает набор утилит для управления, разработки и интеграции с системой. В этом разделе описаны все доступные инструменты, их назначение и примеры использования.

---

## 1. Основной бинарник: `interactive-wallpaper`

Это основной исполняемый файл графического микроядра. Он запускает движок, подключается к Wayland-композитору и начинает рендеринг обоев.

**Синтаксис:**

```bash
interactive-wallpaper [опции]
```

**Опции:**

| Опция | Описание |
|-------|----------|
| `-h`, `--help` | Показать справку и выйти. |
| `--config <путь>` | Использовать указанную директорию конфигурации вместо `~/.config/interactive-wallpaper/`. |
| `--init-workspace` | Создать пользовательскую песочницу (копию плагинов) в `~/.config/interactive-wallpaper/effects/`. |
| `--init-config` | Сгенерировать файлы конфигурации (`init.lua`, `plugins/*.lua`, `providers.lua`) из текущих плагинов. |
| `--list-plugins` | Вывести в JSON список доступных визуальных плагинов и выйти. |
| `--list-providers` | Вывести в JSON список доступных провайдеров данных и выйти. |
| `--inspect <имя>` | Вывести подробную информацию о плагине (параметры, описания) в формате JSON. |

**Примеры:**

```bash
# Запуск с системной конфигурацией
interactive-wallpaper

# Использование кастомной директории
interactive-wallpaper --config ~/my-wallpaper-config

# Инициализация рабочего пространства
interactive-wallpaper --init-workspace

# Просмотр параметров плагина
interactive-wallpaper --inspect "Hilbert Cube"
```

---

## 2. Управление через сокет: `shader-desk-ctl`

Утилита `shader-desk-ctl` предоставляет интерфейс для динамического управления работающим движком через UNIX-сокет. Она позволяет изменять параметры обоев, переключать сцены, управлять провайдерами и выполнять произвольный Lua-код.

### 2.1. Архитектура взаимодействия

Утилита подключается к сокету ядра (по умолчанию `/run/user/<UID>/shader-desk.sock`), отправляет команду, завершённую нулевым байтом (`\0`), и читает ответ до нулевого байта. Таймаут на операции ввода-вывода — 2 секунды.

**Коды возврата:**
- `0` — успех.
- `1` — ошибка (неверный синтаксис, таймаут, недоступный сокет).

### 2.2. Способы передачи кода

**Через аргументы командной строки:**

```bash
shader-desk-ctl "core.get_layer('eDP-1', 'cube'):set('rotation_speed', 0.5)"
```

**Через стандартный ввод (pipe):**

```bash
echo "ctl.toggle('wireframe_mode', '*')" | shader-desk-ctl
cat my_script.lua | shader-desk-ctl
```

Это позволяет передавать многострочные скрипты и использовать сложную логику.

### 2.3. Доступные команды

Полный список команд модуля `ctl` описан в [Lua API справочнике](06-lua-api-reference.md#2-модуль-ctl-внешнее-управление). Здесь приведены наиболее часто используемые:

- **Управление параметрами:**  
  `ctl.set(param, value, [output], [tag])`  
  `ctl.toggle(param, [output], [tag])`  
  `ctl.cycle(param, values_list, [output], [tag])`

- **Пресеты и темы:**  
  `ctl.preset(effect_name, preset_name, [output])`  
  `ctl.pywal([output])`  
  `ctl.theme(mode, [output])`

- **Управление провайдерами:**  
  `ctl.provider(name, enabled)`

- **Анимация и состояние:**  
  `ctl.freeze(state, [output], [tag])`  
  `ctl.flash(color, duration_sec, [output], [tag])`

- **Получение статуса:**  
  `ctl.status()` — выводит JSON с текущим состоянием всех мониторов.

### 2.4. Инжекция произвольного Lua‑кода

Утилита выполняет любой валидный Lua-код в контексте работающего движка. Это позволяет:

- Определять и вызывать пользовательские функции.
- Манипулировать глобальными таблицами.
- Взаимодействовать с BlackBoard напрямую.

**Пример инъекции функции и её вызова:**

```bash
# Определяем функцию
cat <<'EOF' | shader-desk-ctl
function set_all_red()
    for name, _ in pairs(core.outputs) do
        core.get_layer(name, "cube"):set("curve_color", {1.0, 0.0, 0.0})
    end
end
EOF

# Вызываем её
shader-desk-ctl "set_all_red()"
```

### 2.5. Интеграция с оконными менеджерами

В Hyprland (`~/.config/hypr/hyprland.conf`):

```text
bind = $mainMod SHIFT, S, exec, shader-desk-ctl "ctl.toggle('wireframe_mode', '*')"
bind = $mainMod, F, exec, shader-desk-ctl "ctl.flash({1.0,0.0,0.0}, 0.3, '*')"
bind = $mainMod, C, exec, shader-desk-ctl "ctl.cycle('bg_color', {{1,0,0},{0,1,0},{0,0,1}}, '*')"
```

В Sway (`~/.config/sway/config`):

```text
bindsym $mod+Shift+s exec shader-desk-ctl "ctl.toggle('wireframe_mode', '*')"
bindsym $mod+f exec shader-desk-ctl "ctl.flash({1.0,0.0,0.0}, 0.3, '*')"
```

---

## 3. Обёртка для запуска: `shader-desk-run.sh`

Скрипт-обёртка предназначен для запуска движка вместе с фоновыми демонами (audio-daemon и evdev-pointer-daemon). Он гарантирует корректный порядок запуска и корректное завершение всех процессов при остановке.

**Расположение:**  
При системной установке скрипт помещается в `/usr/bin/shader-desk-run`.

**Что делает:**

1. Определяет пути к исполняемым файлам (`interactive-wallpaper`, `audio-daemon`, `evdev-pointer-daemon`) в порядке приоритета: текущая директория → `build-release/` → системный `PATH`.
2. Запускает демоны в фоновом режиме (если они найдены).
3. Запускает `interactive-wallpaper` и передаёт ему все переданные аргументы.
4. При завершении ядра (или при получении `SIGINT`/`SIGTERM`) убивает все фоновые процессы.

**Использование:**

```bash
# Обычный запуск
shader-desk-run

# С кастомной конфигурацией
shader-desk-run --config ~/my-config

# Инициализация рабочего пространства (аргументы передаются ядру)
shader-desk-run --init-workspace
```

**Интеграция с systemd:**

Пользовательский сервис `shader-desk.service` использует именно эту обёртку, что гарантирует корректный запуск демонов при старте сеанса.

---

## 4. Генератор плагинов: `shader-desk-generate`

Утилита `shader-desk-generate` (ранее `generate_plugin.py`) автоматически создаёт скелет C++ плагина на основе GLSL-шейдера с аннотациями. Это позволяет быстро начать разработку нового эффекта без написания шаблонного кода.

**Расположение:**  
При системной установке утилита помещается в `/usr/bin/shader-desk-generate`.

**Синтаксис:**

```bash
shader-desk-generate <путь_к_директории_плагина> [--list-params]
```

- `<путь_к_директории_плагина>` — директория, в которой будет создан плагин. Должна содержать подпапку `shaders/` с хотя бы одним фрагментным шейдером (`.frag` или `.glsl`).
- `--list-params` — вывести распарсенные параметры из шейдера в формате JSON и выйти (без генерации кода).

### 4.1. Поддерживаемые аннотации

**`// @param имя | тип | значение_по_умолчанию | описание`**

Типы: `float`, `int`, `bool`, `vec2`, `vec3`, `vec4`, `string`.

**`// @texture имя_униформы | файл_по_умолчанию | описание`**

Создаёт переменные для работы с текстурой: `GLuint имя_id`, `std::string имя_path`, флаг `имя_pending`, а также методы загрузки через `stb_image`.

### 4.2. Что генерируется

- `CMakeLists.txt` — готовый к сборке.
- `<plugin_name>.hpp` — заголовочный файл класса.
- `<plugin_name>.cpp` — реализация с хуками для Uniform-ов.
- `presets/default.lua` — конфигурация по умолчанию.

### 4.3. Пример использования

```bash
# Создаём директорию плагина
mkdir -p my-effect/shaders

# Пишем шейдер с аннотациями
cat > my-effect/shaders/my_frag.glsl <<'EOF'
#version 300 es
precision highp float;

// @param speed | float | 1.0 | Скорость анимации
// @param color | vec3 | 1.0, 0.0, 0.0 | Основной цвет

uniform float speed;
uniform vec3 color;
out vec4 FragColor;
void main() {
    FragColor = vec4(color * sin(speed * time), 1.0);
}
EOF

# Генерируем плагин
shader-desk-generate my-effect

# Собираем
cd my-effect && mkdir build && cd build && cmake .. && make
```

---


## 6. Связанные разделы

- **[Установка и настройка](01-installation-and-setup.md)** — описание флагов `--init-workspace`, `--init-config` и системной установки.
- **[Lua API справочник](06-lua-api-reference.md#2-модуль-ctl-внешнее-управление)** — полное описание команд модуля `ctl`.
- **[SDK: визуальные плагины](04-sdk-visual-plugins.md)** — подробности о структуре плагинов, генерируемых `shader-desk-generate`.
- **[Рабочее пространство и шейдеры](03-workspaces-and-shaders.md)** — использование песочницы и Hot‑Reload.
