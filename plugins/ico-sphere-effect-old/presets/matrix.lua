-- Preset: Matrix Terminal Green
return {
    shader_theme = "default",
    wireframe_mode = true,
    subdivisions = 4,
    sphere_scale = 1.0,
    background_color = { 0.01, 0.03, 0.01 },   -- Dark-green terminal background
    wireframe_color = { 0.1, 1.0, 0.3 },     -- Phosphor green
    constant_rotation_speed = 0.15,
    noise_amp = 0.08,                          -- Vertex noise (digital glitches)
    oscill_amp = 0.04,                         -- Polygon vibration
    oscill_freq = 15.0                         -- High interference frequency
}