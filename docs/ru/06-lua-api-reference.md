# Справочник Lua API

В архитектуре Shader Desk язык Lua выступает в роли **Control Plane** (управляющей плоскости). В то время как нативный код на C++ и GLSL занимается тяжёлыми вычислениями и рендерингом, Lua управляет маршрутизацией экранов, покадровой анимацией параметров и интеграцией с оконными менеджерами.

Этот документ описывает все доступные методы глобального объекта `core`, вспомогательного модуля `ctl` и Layer Proxy API.

---

## 1. Глобальный объект `core`

Объект `core` автоматически предоставляется движком при старте. Он используется в конфигурационном файле `init.lua` и в сценах.

### 1.1. Маршрутизация мониторов

**`core.outputs`** (таблица)

Определяет соответствие между физическими мониторами Wayland (например, `eDP-1`, `DP-1`) и загруженными сценами. Символ `*` используется как fallback‑правило для всех не назначенных явно экранов.

```lua
core.outputs = {
    ["*"] = core.load_scene("liquid_desktop"),
    ["HDMI-A-1"] = core.load_scene("cyberpunk_city")
}
```

**`core.load_scene(name: string) -> table`**

Загружает файл сцены (`.lua`) по иерархии fallback‑путей:

1. `~/.config/interactive-wallpaper/scenes/<name>.lua`
2. Локальные и системные пути (например, `/usr/share/shader-desk/scenes/`).
3. Встроенная виртуальная файловая система (VFS).

Возвращает таблицу конфигурации слоёв, которая используется в `core.outputs`.

### 1.2. Управление провайдерами данных

**`core.providers`** (таблица)

Таблица для конфигурации C++‑провайдеров (Data Providers). Позволяет включать/отключать фоновый сбор данных и настраивать их параметры.

```lua
core.providers = {
    ["Evdev Pointer Provider"] = {
        enabled = true,
        mouse_sensitivity = 1.0,
        invert_x = false
    }
}
```

Параметры, перечисленные в таблице, автоматически передаются в провайдер через ABI‑метод `set_parameter_abi()`.

### 1.3. Таймеры (асинхронные события)

Таймеры используют системный вызов Linux `timerfd` и интегрированы в `epoll`. Они работают с нулевой нагрузкой на процессор в режиме ожидания.

**`core.set_interval(ms: number, callback: function) -> number`**

Запускает функцию `callback` каждые `ms` миллисекунд. Если `callback` возвращает `nil` или ничего, таймер продолжает работу. Возвращает целочисленный ID таймера.

**Ограничение:** не более 32 активных таймеров одновременно (защита от утечек файловых дескрипторов).

**`core.clear_interval(id: number)`**

Останавливает и удаляет таймер по его ID, освобождая файловый дескриптор в ядре.

```lua
local timer_id = core.set_interval(5000, function()
    print("Tick")
end)
-- позже:
core.clear_interval(timer_id)
```

### 1.4. Прямой доступ к шине данных (BlackBoard API)

BlackBoard — разделяемая память микроядра. Обращение к ней происходит за $O(1)$ без накладных расходов. Это самый быстрый способ обмена данными между Lua, провайдерами и шейдерами.

**Запись:**

- `core.set_string(key: string, value: string)`
- `core.set_float_array(key: string, values: table)`

Максимальный размер массива — **256 элементов**. При попытке записать больше ядро выведет предупреждение и запишет данные в «мусорный» буфер (Trash Buffer), не вызывая краша.

**Чтение:**

- `core.get_float(key: string, default: float) -> float`
- `core.get_string(key: string, default: string) -> string`
- `core.get_float_array(key: string, size: number) -> table`

```lua
-- Запись массива координат
core.set_float_array("grad.positions", { 0.5, 0.5, 0.1, 0.8 })

-- Чтение уровня баса
local bass = core.get_float("audio.bass", 0.0)
```

### 1.5. Глобальная 3D‑камера

**`core.set_camera(pos: table, target: table, up: table, fov: number?)`**

Устанавливает параметры камеры для всех плагинов, наследующих `KinematicEffect`. Параметры:

- `pos` — позиция камеры (таблица из 3 чисел).
- `target` — точка, на которую смотрит камера.
- `up` — вектор «вверх» (по умолчанию `{0, 1, 0}`).
- `fov` — поле зрения в градусах (по умолчанию 45).

Камера активна, пока хотя бы один плагин использует её. При вызове этой функции в BlackBoard устанавливается флаг `scene.camera.active = 1`.

```lua
core.set_camera(
    {3.0, 2.0, 5.0},
    {0.0, 0.0, 0.0},
    {0.0, 1.0, 0.0},
    60.0
)
```

### 1.6. Покадровый хук

**`core.on_frame(callback: function(dt, output_name))`**

Регистрирует функцию, которая будет вызываться перед каждым кадром для каждого активного монитора. Параметры:

- `dt` — дельта времени в секундах (реальное время между кадрами).
- `output_name` — имя монитора (например, `eDP-1`).

Идеально подходит для плавной математической анимации (синусоиды, интерполяции).

```lua
core.on_frame(function(dt, output)
    local time = core.get_float("global_time", 0.0) + dt
    core.set_float_array("global_time", {time})
    local layer = core.get_layer(output, "my_object")
    layer:set("rotation_speed", math.sin(time) * 0.5)
end)
```

### 1.7. Управление слоями (Layer Proxy)

**`core.get_layer(output_name: string, tag: string) -> LayerProxy`**

Возвращает безопасный proxy‑объект для слоя с указанным тегом на указанном мониторе. Если слой не найден, методы proxy будут игнорироваться (без краша).

**Методы LayerProxy:**

- `:set(param: string, value: any) -> LayerProxy`  
  Устанавливает параметр плагина. Поддерживает `boolean`, `number`, `string` и таблицы (для векторов). Возвращает `self` для цепочек вызовов.

- `:set_vec2(param: string, x: number, y: number) -> LayerProxy`  
  Специализированный метод для `vec2` (без аллокаций).

- `:set_vec3(param: string, x: number, y: number, z: number) -> LayerProxy`

- `:set_vec4(param: string, x: number, y: number, z: number, w: number) -> LayerProxy`

- `:get(param: string) -> any`  
  Читает текущее значение параметра из плагина.

```lua
core.get_layer("DP-1", "bg_layer")
    :set("color_top", {1.0, 0.0, 0.0})
    :set("pulse_speed", 2.5)

local color = core.get_layer("DP-1", "bg_layer"):get("color_top")
```

### 1.8. Утилиты

**`core.utils.apply_preset(target: table, plugin_name: string, preset_name: string)`**

Загружает Lua‑пресет из папки `presets/` плагина и рекурсивно сливает его с таблицей `target`, не перезаписывая уже существующие ключи.

Используется внутри сцен для применения предустановок.

**`core.debug`** (таблица)

Зарезервирована для отладочных целей (в настоящее время пуста).

---

## 2. Модуль `ctl` (внешнее управление)

Модуль `ctl.lua` загружается автоматически в `init.lua` и предназначен для интеграции с внешними инструментами (Waybar, Dunst, скрипты bash). Все функции модуля можно вызывать через утилиту `shader-desk-ctl`.

**Синтаксис вызова из терминала:**

```bash
shader-desk-ctl "ctl.method_name(...)"
```

### 2.1. Базовое управление параметрами

**`ctl.set(param: string, value: any, output?: string, tag?: string)`**  
Устанавливает параметр. Если `output = "*"`, применяется ко всем мониторам. `tag` необязателен — используется первый слой монитора.

**`ctl.toggle(param: string, output?: string, tag?: string)`**  
Переключает булев параметр.

**`ctl.cycle(param: string, values_list: table, output?: string, tag?: string)`**  
Циклически переключает параметр по списку значений (поддерживаются числа, строки, цветовые таблицы).

**`ctl.preset(effect_name: string, preset_name: string, output?: string)`**  
Применяет пресет к слою на указанном мониторе (или ко всем, если `output = "*"`).

```bash
shader-desk-ctl "ctl.set('wireframe_mode', false, '*')"
shader-desk-ctl "ctl.toggle('enable_stripes', 'eDP-1')"
shader-desk-ctl "ctl.cycle('bg_color', {{1,0,0}, {0,1,0}, {0,0,1}}, '*')"
shader-desk-ctl "ctl.preset('Icosahedron Sphere', 'cyberpunk', 'DP-1')"
```

### 2.2. Темы и интеграция с Pywal

**`ctl.pywal(output?: string)`**  
Читает файл `~/.cache/wal/colors.json` (создаваемый утилитой `pywal`) и применяет извлечённую палитру к параметрам `background_color`, `wireframe_color`, `curve_color` на указанных мониторах (или на всех).

**`ctl.theme(mode: "dark" | "light", output?: string)`**  
Устанавливает предопределённые тёмные или светлые цвета фона.

```bash
shader-desk-ctl "ctl.pywal('*')"
shader-desk-ctl "ctl.theme('dark', 'eDP-1')"
```

### 2.3. Управление провайдерами

**`ctl.provider(name: string, enabled: boolean)`**  
Включает или отключает провайдера данных на лету.

```bash
shader-desk-ctl "ctl.provider('Native FFTW Audio Provider', false)"
```

### 2.4. Анимация и режимы

**`ctl.freeze(state: boolean, output?: string, tag?: string)`**  
Замораживает анимацию вращения (устанавливает `rotation_speed = 0`) или восстанавливает её, сохраняя предыдущее значение. Полезно для игрового режима или экономии батареи.

**`ctl.flash(color: table, duration_sec: number, output?: string, tag?: string)`**  
Временно изменяет цвет (`wireframe_color` или `curve_color`) на указанный, а через заданное время плавно возвращает исходный. Использует аппаратный таймер.

```bash
shader-desk-ctl "ctl.freeze(true, '*')"
shader-desk-ctl "ctl.flash({1.0, 0.0, 0.0}, 0.5, '*')"
```

### 2.5. Статус для Waybar / Eww

**`ctl.status()`**  
Выводит в stdout JSON‑строку с текущим состоянием мониторов и их основными слоями.

```bash
shader-desk-ctl "ctl.status()"
# Вывод: {"DP-1":{"primary_layer":"cyberpunk_city"},"eDP-1":{"primary_layer":"liquid"}}
```

---

## 3. Примеры интеграции

### 3.1. Горячие клавиши в Hyprland

В `~/.config/hypr/hyprland.conf`:

```text
bind = $mainMod SHIFT, S, exec, shader-desk-ctl "ctl.toggle('wireframe_mode', '*')"
bind = $mainMod, F, exec, shader-desk-ctl "ctl.flash({1.0,0.0,0.0}, 0.3, '*')"
```

### 3.2. Модуль Waybar

Добавьте в конфиг Waybar пользовательский модуль:

```json
"custom/shader-desk": {
    "exec": "shader-desk-ctl 'ctl.status()'",
    "return-type": "json",
    "interval": 5
}
```

### 3.3. Инъекция произвольного Lua‑скрипта

```bash
cat <<'EOF' | shader-desk-ctl
local layer = core.get_layer("eDP-1", "planet")
layer:set_vec3("offset", 0.0, 2.0, 0.0)
print("Planet moved up!")
EOF
```

---

## 4. Связанные разделы

- **[Конфигурация и сцены](02-configuration-and-scenes.md)** — подробное описание структуры конфигурационных файлов.
- **[Архитектура: BlackBoard](07-architecture-overview.md#4-blackboard-шина-памяти-и-trash-buffer)** — устройство разделяемой памяти.
- **[SDK: провайдеры данных](05-sdk-data-providers.md)** — создание собственных источников данных.
- **[Утилиты командной строки](08-utilities-reference.md)** — полное описание `shader-desk-ctl` и других утилит.
