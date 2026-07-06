-- ~/.config/interactive-wallpaper/ctl.lua
-- Модуль вспомогательных функций для управления через shader-desk-ctl

local ctl = {}

-- Вспомогательная функция: применяет действие к указанному монитору или ко ВСЕМ ("*")
local function with_target(output_name, action)
    if not output_name or output_name == "*" or output_name == "" then
        for name, _ in pairs(core.outputs) do
            action(name, core.outputs[name])
        end
    else
        if core.outputs[output_name] then
            action(output_name, core.outputs[output_name])
        end
    end
end

-- ==============================================================================
-- 1. УМНОЕ УПРАВЛЕНИЕ ПАРАМЕТРАМИ (Smart Control)
-- ==============================================================================

-- Установка параметра (по умолчанию для всех мониторов)
-- Пример: shader-desk-ctl "ctl.set('sphere_scale', 1.5)"
function ctl.set(param, value, output)
    with_target(output, function(name, out)
        out.settings[param] = value
        core.set_effect_param(name, param, value)
    end)
end

-- Инвертирование булевого параметра
-- Пример: shader-desk-ctl "ctl.toggle('wireframe_mode')"
function ctl.toggle(param, output)
    with_target(output, function(name, out)
        out.settings[param] = not out.settings[param]
        core.set_effect_param(name, param, out.settings[param])
    end)
end

-- Вспомогательная функция для глубокого сравнения (понимает таблицы и цвета)
local function is_equal(a, b)
    if type(a) ~= type(b) then return false end
    if type(a) == "table" then
        if #a ~= #b then return false end
        for k, v in pairs(a) do
            if not is_equal(v, b[k]) then return false end
        end
        return true
    end
    return a == b
end

-- Циклическое переключение (с поддержкой цветов {R,G,B} и строк!)
function ctl.cycle(param, values_list, output)
    with_target(output, function(name, out)
        local current = out.settings[param]
        local next_idx = 1
        for i, val in ipairs(values_list) do
            -- Используем is_equal вместо обычного ==
            if is_equal(val, current) then
                next_idx = (i % #values_list) + 1
                break
            end
        end
        local new_val = values_list[next_idx]
        out.settings[param] = new_val
        core.set_effect_param(name, param, new_val)
    end)
end

-- Смена пресета на лету
-- Пример: shader-desk-ctl "ctl.preset('Icosahedron Sphere', 'cyberpunk')"
function ctl.preset(effect_name, preset_name, output)
    with_target(output, function(name, out)
        if core.utils.apply_preset then
            core.utils.apply_preset(out.settings, effect_name, preset_name)
            -- Применяем все загруженные параметры в C++
            for k, v in pairs(out.settings) do
                core.set_effect_param(name, k, v)
            end
        end
    end)
end

-- ==============================================================================
-- 2. ИНТЕГРАЦИЯ С ТЕМАМИ И PYWAL (Theming & Unixporn)
-- ==============================================================================

-- Автоматическая подгрузка цветовой палитры из Pywal (~/.cache/wal/colors.json)
-- Пример: shader-desk-ctl "ctl.pywal()" (можно вставить в хук запуска wal)
function ctl.pywal(output)
    local wal_file = os.getenv("HOME") .. "/.cache/wal/colors.json"
    local f = io.open(wal_file, "r")
    if not f then 
        print("Pywal colors.json not found!") 
        return 
    end
    local content = f:read("*all")
    f:close()

    -- Простой парсер HEX цветов из JSON в формат {R, G, B}
    local function hex_to_rgb(hex)
        hex = hex:gsub("#", "")
        return {
            tonumber("0x" .. hex:sub(1, 2)) / 255.0,
            tonumber("0x" .. hex:sub(3, 4)) / 255.0,
            tonumber("0x" .. hex:sub(5, 6)) / 255.0
        }
    end

    local bg_hex = content:match('"color0": "%s*(#[%a%d]+)')
    local fg_hex = content:match('"color7": "%s*(#[%a%d]+)')
    local accent_hex = content:match('"color5": "%s*(#[%a%d]+)')

    if bg_hex and accent_hex then
        ctl.set("background_color", hex_to_rgb(bg_hex), output)
        ctl.set("wireframe_color", hex_to_rgb(accent_hex), output)
        ctl.set("wave_color", hex_to_rgb(accent_hex), output)
        print("Successfully applied Pywal palette!")
    end
end

-- Переключение дневной/ночной темы
-- Пример: shader-desk-ctl "ctl.theme('dark')"
function ctl.theme(mode, output)
    if mode == "dark" then
        ctl.set("bloom_intensity", 0.6, output)
        ctl.set("background_color", {0.05, 0.05, 0.08}, output)
    elseif mode == "light" then
        ctl.set("bloom_intensity", 0.0, output)
        ctl.set("background_color", {0.85, 0.90, 0.95}, output)
    end
end

-- ==============================================================================
-- 3. УПРАВЛЕНИЕ РЕСУРСАМИ И ИГРОВОЙ РЕЖИМ (Power & Gaming)
-- ==============================================================================

-- Включение/выключение реакции на аудио или мышь
-- Пример: shader-desk-ctl "ctl.provider('Cava Audio Provider', false)" (перед созвоном в Zoom)
function ctl.provider(name, enabled)
    if core.providers[name] then
        core.providers[name].enabled = enabled
        print("Provider " .. name .. " enabled: " .. tostring(enabled))
    end
end

-- "Игровой режим" или режим экономии батареи (Замораживает анимацию)
-- Пример: shader-desk-ctl "ctl.freeze(true)"
function ctl.freeze(state, output)
    with_target(output, function(name, out)
        if state then
            out._saved_speed = out.settings.speed or 1.0
            out._saved_rot = out.settings.constant_rotation_speed or 0.1
            ctl.set("speed", 0.0, name)
            ctl.set("constant_rotation_speed", 0.0, name)
            ctl.set("oscill_amp", 0.0, name)
        else
            ctl.set("speed", out._saved_speed or 1.0, name)
            ctl.set("constant_rotation_speed", out._saved_rot or 0.1, name)
        end
    end)
end

-- ==============================================================================
-- 4. ИНТЕРАКТИВНЫЕ ЭФФЕКТЫ И УВЕДОМЛЕНИЯ (Notifications & Events)
-- ==============================================================================

-- Вспышка цветом при получении уведомления от Dunst / Mako / SwayNC
-- Пример: shader-desk-ctl "ctl.flash({1.0, 0.0, 0.0}, 0.5)" (вспышка красным на полсекунды)
function ctl.flash(flash_color, duration_sec, output)
    with_target(output, function(name, out)
        local orig_color = out.settings.wireframe_color or {0.5, 0.5, 0.7}
        ctl.set("wireframe_color", flash_color, name)
        
        -- Используем таймер из твоего ядра для возврата цвета через X миллисекунд
        core.set_interval(math.floor(duration_sec * 1000), function()
            ctl.set("wireframe_color", orig_color, name)
            return nil -- Останавливаем таймер
        end)
    end)
end

-- Импульс от удара (Временный скачок размера/амплитуды)
-- Пример: shader-desk-ctl "ctl.pulse(2.5)"
function ctl.pulse(target_scale, output)
    with_target(output, function(name, out)
        local orig_scale = out.settings.sphere_scale or 1.0
        ctl.set("sphere_scale", target_scale, name)
        
        core.set_interval(150, function()
            ctl.set("sphere_scale", orig_scale, name)
            return nil
        end)
    end)
end

-- ==============================================================================
-- 5. ИНТЕГРАЦИЯ С WAYBAR / EWW (Status Output)
-- ==============================================================================

-- Вывод статуса для отображения в панелях (в формате JSON)
-- Пример: shader-desk-ctl "ctl.status()"
function ctl.status()
    local result = "{"
    for name, out in pairs(core.outputs) do
        local effect = out.effect or core.default_effect
        result = result .. string.format('"%s": {"effect": "%s"}, ', name, effect)
    end
    result = result:sub(1, -3) .. "}\n"
    io.write(result)
end

-- Вывод списка всех подключенных в данный момент к мониторам эффектов
function ctl.list_active()
    local res = {}
    for output_name, out in pairs(core.outputs) do
        table.insert(res, {
            output = output_name,
            effect = out.effect or core.default_effect,
            preset = out.preset or "none"
        })
    end
    -- Выводим как JSON для удобного парсинга скриптами
    print(require("json").encode(res)) 
end

-- Получить текущие (живые) значения параметров на мониторе
function ctl.get_settings(output_name)
    local target = output_name or "eDP-1"
    local out = core.outputs[target]
    if out and out.settings then
        -- Выплевываем текущий стейт таблицы настроек
        for k, v in pairs(out.settings) do
            if type(v) ~= "function" then
                print(string.format("%s = %s", k, tostring(v)))
            end
        end
    else
        print("ERROR: Output or settings not found")
    end
end

return ctl
