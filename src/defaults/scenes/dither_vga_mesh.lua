local M = {
    meta = { name = "VGA 256 Procedural Dunes" },
    state = { init = false, time = 0.0 }
}

local function apply_palette()
    -- 6 цветов (Синтвейв закат: от темно-синего до ярко-желтого)
    local hex_colors = {"#0a0512", "#221133", "#4d1a59", "#b52c66", "#f26d5b", "#f9c16b"}
    local flat_rgb = {}
    for _, hex in ipairs(hex_colors) do
        table.insert(flat_rgb, tonumber(hex:sub(2, 3), 16) / 255.0)
        table.insert(flat_rgb, tonumber(hex:sub(4, 5), 16) / 255.0)
        table.insert(flat_rgb, tonumber(hex:sub(6, 7), 16) / 255.0)
    end
    core.set_float_array("dither.palette", flat_rgb)
end

M.layers = {
    {
        effect = "Solid Bg",
        tag = "mesh_bg",
        settings = {
            gradient_type = 3, -- 4-Corner Mesh
            color_space = 1,   -- OKLab для идеального смешивания
            -- Базовые цвета для градиента (они превратятся в черно-белую карту высот)
            color_1 = {0.1, 0.1, 0.1}, 
            color_2 = {0.8, 0.8, 0.8},
            color_3 = {0.3, 0.3, 0.3},
            color_4 = {0.6, 0.6, 0.6},
        }
    },
    {
        effect = "Dither Effect",
        tag = "vga_dither",
        postprocess = true,
        settings = {
            downsample_scale = 3.0,
            dither_spread = 0.6,
            bayer_size = 2,     -- 8x8 матрица для богатых градиентов
            colors_count = 6    -- Используем 6 цветов из палитры
        }
    }
}

M.on_frame = function(self, dt, output_name)
    if not self.state.init then
        apply_palette()
        self.state.init = true
    end

    self.state.time = self.state.time + dt
    local t = self.state.time * 0.3

    -- Плавно меняем цвета градиента, чтобы "облака" двигались
    local val1 = 0.2 + math.sin(t) * 0.2
    local val2 = 0.7 + math.cos(t * 0.8) * 0.3
    
    core.get_layer(output_name, "mesh_bg")
        :set_vec3("color_1", val1, val1, val1)
        :set_vec3("color_2", val2, val2, val2)
end

return M