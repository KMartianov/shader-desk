-- Пресет: Architectural Minimalism
local time_acc = {}

return {
    shader_theme = "harmonics",
    wireframe_mode = true,
    subdivisions = 1,
    sphere_scale = 0.9,
    background_color = { 0.85, 0.90, 0.95 },
    wireframe_color = { 0.02, 0.02, 0.02 },
    constant_rotation_speed = 0.04,
    min_rotation_speed = 0.01,
    rotation_decay = 0.94,
    oscill_amp = 0.0,
    wave_amp = 0.0,
    noise_amp = 0.0,                           -- <--- ЗДЕСЬ БЫЛА ПРОПУЩЕНА ЗАПЯТАЯ!

    -- 2. ПОКАДРОВАЯ ЛОГИКА ПРЕСЕТА
    on_frame = function(dt, output_name)
        if dt > 0.1 then dt = 0.0166 end
        
        -- Инициализируем и обновляем таймер конкретного экрана
        time_acc[output_name] = (time_acc[output_name] or 0.0) + dt
        
        local cycle_speed = 0.5
        local t = time_acc[output_name] * cycle_speed
        
        -- Расчет RGB (сдвиг фаз 120 градусов)
        local r = 0.65 + 0.35 * math.sin(t)
        local g = 0.65 + 0.35 * math.sin(t + 2.094395)
        local b = 0.65 + 0.35 * math.sin(t + 4.188790)
        
        -- Комплементарный фон (сдвиг 180 градусов)
        local bg_r = 0.06 + 0.04 * math.sin(t + 3.141592)
        local bg_g = 0.06 + 0.04 * math.sin(t + 5.235987)
        local bg_b = 0.06 + 0.04 * math.sin(t + 1.047197)

        -- Отправляем в C++ Wayland Compositor
        core.set_effect_param(output_name, "wireframe_color", { r, g, b })
        core.set_effect_param(output_name, "background_color", { bg_r, bg_g, bg_b })
    end
}
