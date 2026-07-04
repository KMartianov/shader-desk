-- Пресет: Matrix Terminal Green
return {
    shader_theme = "default",
    wireframe_mode = true,
    subdivisions = 4,
    sphere_scale = 1.0,
    background_color = { 0.01, 0.03, 0.01 },   -- Темно-зеленый терминальный фон
    wireframe_color = { 0.1, 1.0, 0.3 },     -- Фосфорный зеленый
    constant_rotation_speed = 0.15,
    noise_amp = 0.08,                          -- Шум вершин (цифровые глитчи)
    oscill_amp = 0.04,                         -- Вибрация полигонов
    oscill_freq = 15.0                         -- Высокая частота помех
}