-- ==============================================================================
-- SHADER DESK: ГЛАВНЫЙ КОНФИГУРАЦИОННЫЙ ФАЙЛ (init.lua)
-- ==============================================================================
-- Этот файл поддерживает горячую перезагрузку (Hot-Reload). 
-- Просто сохраните изменения, и обои обновятся мгновенно без перезапуска ядра.

-- Загружаем вспомогательный модуль для CLI-управления (shader-desk-ctl)
_G.ctl = require("ctl")

core = core or {}

-- Эффект по умолчанию (если монитор не описан явно в core.outputs)
core.default_effect = "Icosahedron Sphere Old" 
core.interactive = true

-- ==============================================================================
-- 1. НАСТРОЙКИ МОНИТОРОВ И СЛОЕВ (Multi-Monitor & Multi-Layer Routing)
-- ==============================================================================
-- Узнать имена ваших мониторов можно командой `wlr-randr` или `hyprctl monitors`.
-- Обычно это eDP-1 (ноутбук), DP-1, HDMI-A-1 и т.д.

core.outputs = {
    -- ---------------------------------------------------------
    -- ЭКРАН 1: Ноутбук (Сфера с пресетом и анимацией орбиты)
    -- ---------------------------------------------------------
    ["eDP-1"] = {
        -- fbo_scale = 0.75, -- Можно снизить разрешение рендера для оптимизации (опционально)
        fps_limit = 60.0,
        
        layers = {
            -- СЛОЙ 1: Задний фон (Отрисовывается первым)
            {
                effect = "Universal Background",
                settings = {
                    gradient_type = 2, -- Радиальный градиент
                    color_top = { 0.5, 0.01, 0.08 },
                    color_bottom = { 1.01, 0.0, 0.02 },
                    pulse_speed = 0.5
                }
            },
            -- СЛОЙ 2: 3D Объект поверх фона
            {
                effect = "Icosahedron Sphere Old",
                preset = "cyberpunk", -- Автоматически загрузит настройки из пресета
                settings = {
                    sphere_scale = 1.0,
                    -- Поскольку фон теперь рисует Universal Background, 
                    -- wireframe_color мы будем анимировать через Lua ниже!
                }
            }
        }
    },

    -- ---------------------------------------------------------
    -- ЭКРАН 2: Внешний монитор (Куб Гильберта с RGB-переливанием)
    -- ---------------------------------------------------------
    ["DP-1"] = {
        layers = {
            -- СЛОЙ 1: Фон (Вертикальный градиент)
            {
                effect = "Universal Background",
                settings = {
                    gradient_type = 1, -- 1 = Вертикальный
                    color_top = { 0.02, 0.03, 0.05 },
                    color_bottom = { 0.05, 0.08, 0.12 }
                }
            },
            -- СЛОЙ 2: Куб Гильберта
            {
                effect = "Hilbert Cube",
                settings = {
                    hilbert_order = 3,       -- Детализация фрактала
                    draw_cube_outline = true,
                    cube_color = {0.2, 0.2, 0.3},
                    rotation_decay = 0.98    -- Долгое затухание вращения от мыши
                }
            }
        }
    }
}

-- ==============================================================================
-- 2. НАСТРОЙКИ ПРОВАЙДЕРОВ ДАННЫХ (Smart Providers)
-- ==============================================================================
-- Провайдеры собирают системные данные (мышь, звук) и отправляют их в шейдеры.
core.providers = {
    ["Evdev Pointer Provider"] = {
        enabled = true,
        mouse_sensitivity = 1.0,
        touchpad_sensitivity = 2.5,
    },
    ["Cava Audio Provider"] = {
        enabled = true,
        smoothing = 0.85,          -- Сглаживание падения столбиков эквалайзера
        volume_multiplier = 1.0,
        bass_multiplier = 1.5,     -- Усиливаем реакцию на бас
        treble_multiplier = 2.0
    }
}


-- ==============================================================================
-- 3. ГЛОБАЛЬНЫЙ ДИСПЕТЧЕР АНИМАЦИЙ (Animation Router)
-- ==============================================================================
-- Эта функция вызывается ядром КАЖДЫЙ КАДР для каждого активного монитора.
-- Здесь мы реализуем кастомную логику (орбиты, RGB-подсветку и т.д.)

local time_acc = {} -- Таблица для хранения времени каждого монитора

core.on_frame(function(dt, output_name)
    -- Инициализируем таймер для конкретного экрана (защита от скачков dt)
    if dt > 0.1 then dt = 0.016 end
    time_acc[output_name] = (time_acc[output_name] or 0.0) + dt
    local t = time_acc[output_name]

    -- ---------------------------------------------------------
    -- АНИМАЦИЯ ДЛЯ ЭКРАНА 1 (eDP-1): Фигуры Лиссажу ("Орбита")
    -- ---------------------------------------------------------
    if output_name == "eDP-1" then
        -- Движение по плавной восьмерке (псевдо-3D полет)
        local orbit_speed = 0.5
        local radius_x = 0.8
        local radius_y = 0.4
        
        local offset_x = math.sin(t * orbit_speed) * radius_x
        local offset_y = math.sin(t * orbit_speed * 2.0) * radius_y
        
        -- Передаем новые координаты в C++ плагин
        core.set_effect_param(output_name, "offset", { offset_x, offset_y, 0.0 })
        
        -- Пульсация толщины/цвета в зависимости от позиции
        local glow = 0.5 + 0.5 * math.cos(t * orbit_speed)
        core.set_effect_param(output_name, "wireframe_color", { 1.0, glow * 0.5, 0.55 })
    end

    -- ---------------------------------------------------------
    -- АНИМАЦИЯ ДЛЯ ЭКРАНА 2 (DP-1): RGB Радуга
    -- ---------------------------------------------------------
    if output_name == "DP-1" then
        local rgb_speed = 1.2
        
        -- Математика плавного смещения фаз для RGB (120 градусов)
        local r = 0.5 + 0.5 * math.sin(t * rgb_speed)
        local g = 0.5 + 0.5 * math.sin(t * rgb_speed + 2.094) -- +120°
        local b = 0.5 + 0.5 * math.sin(t * rgb_speed + 4.188) -- +240°
        
        core.set_effect_param(output_name, "curve_color", { r, g, b })
        
        -- Легкое покачивание самого куба (дыхание)
        local breathe = math.sin(t) * 0.1
        core.set_effect_param(output_name, "offset", { 0.0, breathe, 0.0 })
    end

    -- ---------------------------------------------------------
    -- ПОДДЕРЖКА ПРЕСЕТОВ
    -- ---------------------------------------------------------
    -- Если внутри пресета была задана своя функция on_frame, вызываем её
    local out_conf = core.outputs[output_name]
    if out_conf and out_conf.settings and type(out_conf.settings.on_frame) == "function" then
        out_conf.settings.on_frame(dt, output_name)
    end
end)
