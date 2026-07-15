-- ==============================================================================
-- SCENE: Cyber Horizon
-- Линейный неоновый градиент. Медленно вращается на 360 градусов.
-- Демонстрирует идеальный блендинг несовместимых цветов через OKLab.
-- ==============================================================================

local M = {
    meta = {
        name = "Cyber Horizon",
        author = "Shader Desk",
        version = "1.0",
    },
    fps_limit = 60.0, 

    layers = {
        {
            effect = "Solid Bg",
            tag = "horizon_bg",
            settings = {
                gradient_type = 1,       -- Linear Angle
                color_space = 1,         -- OKLab
                dither_strength = 1.0,   
                
                angle = 45.0,            -- Начальный угол
                
                color_1 = {0.95, 0.25, 0.15}, -- Neon Orange/Red
                color_2 = {0.15, 0.05, 0.45}, -- Deep Cyber Violet
            }
        },
        {
            effect = "Hilbert Cube",
            tag = "cube", -- Semantic alias for the Fluent API
            settings = { 
                hilbert_order = 3, 
                draw_cube_outline = true, 
                rotation_decay = 0.95 
            }
        },
    },
    
    state = { angle = 45.0 }
}

M.on_frame = function(self, dt, output_name)
    -- Вращаем градиент со скоростью 5 градусов в секунду
    self.state.angle = self.state.angle + (5.0 * dt)
    if self.state.angle > 360.0 then
        self.state.angle = self.state.angle - 360.0
    end

    -- Читаем басы из audio-provider (если музыка играет)
    local bass = core.get_float("audio.bass", 0.0)

    -- Если есть бас, градиент слегка "вспыхивает" (смещаем фиолетовый в сторону розового)
    local r_boost = 0.15 + (bass * 0.3)
    
    core.get_layer(output_name, "horizon_bg")
        :set("angle", self.state.angle)
        :set_vec3("color_2", r_boost, 0.05, 0.45)
end

return M