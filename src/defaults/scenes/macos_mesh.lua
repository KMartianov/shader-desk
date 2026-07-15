-- ==============================================================================
-- SCENE: MacOS Mesh
-- Мягкий 4-точечный градиент в цветовом пространстве OKLab.
-- Цвета медленно пульсируют, создавая эффект "живого" премиального фона.
-- ==============================================================================

local M = {
    meta = {
        name = "MacOS Mesh",
        author = "Shader Desk",
        version = "1.0",
    },
    fps_limit = 30.0, -- Для фона достаточно 30 FPS, экономим батарею

    layers = {
        {
            effect = "Solid Bg",
            tag = "mesh_bg",
            settings = {
                gradient_type = 3,       -- 4-Corner Mesh
                color_space = 1,         -- OKLab (Идеальное смешивание)
                dither_strength = 1.0,   -- Включаем TPDF анти-бэндинг
                
                -- Базовая палитра (Пастельные тона)
                color_1 = {0.85, 0.35, 0.55}, -- Top-Left (Pink)
                color_2 = {0.25, 0.55, 0.95}, -- Bottom-Right (Blue)
                color_3 = {0.95, 0.75, 0.35}, -- Top-Right (Peach)
                color_4 = {0.45, 0.25, 0.85}, -- Bottom-Left (Purple)
            }
        }
    },
    
    state = { time = 0.0 }
}

M.on_frame = function(self, dt, output_name)
    self.state.time = self.state.time + dt
    local t = self.state.time * 0.2 -- Очень медленное течение времени

    -- Слегка модулируем яркость и насыщенность каждого угла для эффекта "дыхания"
    -- Zero-Allocation вызовы: используем set_vec3 вместо создания новых таблиц
    core.get_layer(output_name, "mesh_bg")
        :set_vec3("color_1", 0.85 + math.sin(t)*0.1,  0.35,  0.55)
        :set_vec3("color_2", 0.25,  0.55 + math.cos(t*1.2)*0.1,  0.95)
        :set_vec3("color_3", 0.95,  0.75,  0.35 + math.sin(t*0.8)*0.1)
        :set_vec3("color_4", 0.45 + math.cos(t*0.9)*0.1,  0.25,  0.85)
end

return M