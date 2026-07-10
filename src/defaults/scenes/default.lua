-- ==============================================================================
-- Сцена: Default (Hilbert Quantum Cube)
-- ==============================================================================
return {
    meta = {
        name = "Quantum Cube",
        author = "Shader Desk",
        version = "1.0",
        dependencies = { "Universal Background", "Hilbert Cube" }
    },
    
    fps_limit = 60.0,
    
    layers = {
        {
            effect = "Universal Background",
            settings = { gradient_type = 1, color_top = { 0.02, 0.03, 0.05 }, color_bottom = { 0.05, 0.08, 0.12 } }
        },
        {
            effect = "Hilbert Cube",
            settings = { hilbert_order = 5, draw_cube_outline = true, rotation_decay = 0.98 }
        },
    },
    
    state = { time = 0.0, color = {0.0, 0.0, 0.0}, offset = {0.0, 0.0, 0.0} },
    
    on_frame = function(self, dt, output_name)
        self.state.time = self.state.time + dt
        local t = self.state.time
        local rgb_speed = 1.2
        
        self.state.color[1] = 0.5 + 0.5 * math.sin(t * rgb_speed)
        self.state.color[2] = 0.5 + 0.5 * math.sin(t * rgb_speed + 2.094)
        self.state.color[3] = 0.5 + 0.5 * math.sin(t * rgb_speed + 4.188)
        core.set_effect_param(output_name, "curve_color", self.state.color)
        
        self.state.offset[2] = math.sin(t) * 0.1
        core.set_effect_param(output_name, "offset", self.state.offset)
    end
}