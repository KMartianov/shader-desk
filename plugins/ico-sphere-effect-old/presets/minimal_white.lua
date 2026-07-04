-- Пресет: Architectural Minimalism
return {
    shader_theme = "default",
    wireframe_mode = true,
    subdivisions = 5,                          -- Плотная геодезическая сетка
    sphere_scale = 0.9,
    background_color = { 0.02, 0.02, 0.02 },   -- Строгий глубокий черный
    wireframe_color = { 0.85, 0.90, 0.95 },  -- Серебристо-белый хрустящий цвет
    constant_rotation_speed = 0.04,            -- Едва заметное медитативное вращение
    min_rotation_speed = 0.01,
    rotation_decay = 0.94,
    oscill_amp = 0.0,                          -- Полное отсутствие деформаций
    wave_amp = 0.0,
    noise_amp = 0.0
}