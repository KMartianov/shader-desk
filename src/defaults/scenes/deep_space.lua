-- ==============================================================================
-- SCENE: Deep Space Parallax
-- Темный радиальный градиент. Источник света интерактивно реагирует на мышь.
-- Демонстрация Zero-Latency интеграции с pointer-provider.
-- ==============================================================================

local M = {
    meta = {
        name = "Deep Space Parallax",
        author = "Shader Desk",
        version = "1.0",
    },
    fps_limit = 60.0, -- Выше FPS для плавной реакции на мышь

    layers = {
        {
            effect = "Solid Bg",
            tag = "space_bg",
            settings = {
                gradient_type = 2,       -- Radial Offset
                color_space = 1,         -- OKLab
                dither_strength = 1.0,   
                
                radial_radius = 1.5,
                radial_center = {0.5, 0.5},
                
                color_1 = {0.12, 0.15, 0.25}, -- Center (Soft dark blue glow)
                color_2 = {0.02, 0.02, 0.04}, -- Edges (Almost pitch black)
            }
        }
    },
    
    state = { 
        current_x = 0.5, 
        current_y = 0.5 
    }
}

M.on_frame = function(self, dt, output_name)
    -- Читаем сырые дельты мыши из шины BlackBoard (записываются демоном pointer-provider)
    local mx = core.get_float("mouse.accum_x", 0.0)
    local my = core.get_float("mouse.accum_y", 0.0)

    -- Превращаем бесконечные дельты мыши в ограниченное смещение [-0.3 .. 0.3] от центра
    local target_x = 0.5 + math.sin(mx * 0.005 * 1000.0) * 0.3
    local target_y = 0.5 + math.sin(my * 0.005 * 1000.0) * 0.3

    -- Плавная интерполяция (Lerp) для эффекта кинетической инерции
    -- Независимо от того, как резко дернули мышь, свечение догонит плавно
    self.state.current_x = self.state.current_x + (target_x - self.state.current_x) * 5.0 * dt
    self.state.current_y = self.state.current_y + (target_y - self.state.current_y) * 5.0 * dt

    core.get_layer(output_name, "space_bg")
        :set_vec2("radial_center", self.state.current_x, self.state.current_y)
end

return M