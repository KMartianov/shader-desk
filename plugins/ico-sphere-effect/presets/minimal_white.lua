-- Preset: Architectural Minimalism
return {
    shader_theme = "default",
    wireframe_mode = true,
    subdivisions = 5,                          -- Dense geodesic grid
    sphere_scale = 0.9,
    background_color = { 0.02, 0.02, 0.02 },   -- Strict deep black
    wireframe_color = { 0.85, 0.90, 0.95 },  -- Silvery-white crisp color
    constant_rotation_speed = 0.04,            -- Barely noticeable meditative rotation
    min_rotation_speed = 0.01,
    rotation_decay = 0.94,
    oscill_amp = 0.0,                          -- Complete absence of deformations
    wave_amp = 0.0,
    noise_amp = 0.0
}