local M = {}

M.layers = {
    {
        effect = "Gradient Bg",
        settings = {
            blend_power = 0.5,
            bg_color = {0.05, 0.05, 0.08},
            enable_stripes = false,
            stripes_density = 15.0,
            stripes_opacity = 0.2,
            dithering_amount = 0.04
        }
    },
    {
            effect = "Hilbert Cube",
            settings = { hilbert_order = 5, draw_cube_outline = true, rotation_decay = 0.98 }
        },
    
}

M.on_frame = function(self, dt, output_name)
    local t = core.time or 0
    core.time = t + dt


    -- Динамика точек (Крутятся по орбитам)
    local p1_x = 0.5 + math.sin(t * 10.8) * 0.4
    local p1_y = 0.5 + math.cos(t * 0.8) * 0.4
    
    local p2_x = 0.5 + math.sin(t * 0.5 + 3.14) * 0.3
    local p2_y = 0.5 + math.cos(t * 0.5 + 3.14) * 0.3

    -- Реакция на мышь
    local mx = 0.0
    local my = 0.0
    
    -- Реакция на бас
    local bass = core.get_float("audio.bass", 0.0)

    -- Отправляем массивы в ядро
    core.set_float_array("grad.positions", { 
        p1_x, p1_y, 
        p2_x, p2_y,
        0.5 + mx, 0.5 + my,  -- Точка 3 следует за мышью
        0.5 , 0.0
    })
    
    core.set_float_array("grad.colors", { 
        1.0, 1.0, 1.0,  -- Pink
        0.3, 0.0, 1.0,  -- Cyan
        0.0, 1.0, 0.5,   -- Lime (мышь)
        1.0, 0.0, 0.0   -- Lime (мышь)
    })

    core.set_float_array("grad.radii", { 
        0.6, 
        0.5, 
        0.3 + bass * 0.5, -- Точка мыши пульсирует от баса!
        0.3
    })
    
    -- Включаем 3 точки
    core.set_effect_param(output_name, "point_count", 4)
end

return M
