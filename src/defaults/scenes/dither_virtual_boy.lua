local M = {
    meta = { name = "Virtual Boy Dither & Audio Sphere" },
    state = { init = false, time = 0.0 }
}

local function apply_palette()
    -- Всего 2 цвета: Глубокий черный и Неоновый красный
    local hex_colors = {"#050505", "#ff003c"} 
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
        effect = "Image Bg",
        tag = "bg",
        settings = {
            base_image_path = "img/img2.jpg", -- Киберпанк девушка
            fill_mode = 0,
            contrast = 1.5, -- Повышаем контраст перед дизерингом
        }
    },
    {
        effect = "Dither Effect",
        tag = "dither",
        postprocess = true,
        settings = {
            downsample_scale = 10.0, -- Мелкий пиксель
            dither_spread = 1.0,    -- Агрессивный шум для 1-Bit
            bayer_size = 0,         -- 2x2 матрица (более жесткая)
            colors_count = 2        -- Строго 2 цвета
        }
    },
    {
        effect = "Icosahedron Sphere Old",
        tag = "sphere",
        --postprocess = true,
        settings = {
            shader_theme = "harmonics",
            wireframe_mode = true,
            subdivisions = 2,
            sphere_scale = 0.8,
            wireframe_color = {1.0, 0.0, 1.00}, -- #ff003c
            object_color = {0.02, 0.02, 0.02},
            rotation_decay = 0.95
        }
    },
   --  {
   --          effect = "Postprocess Effect", -- Накидываем сверху глитч
   --          tag = "glitch_filter",
   --          postprocess = true,         -- КРИТИЧЕСКИ ВАЖНО! Включит Ping-Pong FBO
   --          settings = {
   --        shader_theme = "kaleidoscope",
   --  variant = 0,     
   --  intensity = 0.1, -- Легкий зум
   --  scale = 10.0,     -- Шестиугольная снежинка
   --  speed = 0.2      -- Медленное гипнотическое вращение
   --          }
   --  },
             {
             effect = "Postprocess Effect", -- Накидываем сверху глитч
             tag = "glitch_filter",
             postprocess = true,         -- КРИТИЧЕСКИ ВАЖНО! Включит Ping-Pong FBO
             settings = {
           shader_theme = "liquid_glass",
     variant = 1,     -- Режим блочного датамоша
     intensity = 0.15,-- Чем выше, тем больше экран разваливается на квадраты
     scale = 5.0,    -- Плотность макро-блоков (размер квадратов)
     speed = 1.0,     -- Частота "поломки" (зависит от BPM, если захочешь привязать к музыке)
             }
         },
 --           {
 --           effect = "Postprocess Effect", -- Накидываем сверху глитч
 --           tag = "glitch_filter",
 --           postprocess = true,         -- КРИТИЧЕСКИ ВАЖНО! Включит Ping-Pong FBO
 --           settings = {
 --   variant = 1,     
 --   intensity = 0.1, -- Легкий зум
 --   scale = 10.0,     -- Шестиугольная снежинка
 --   speed = 0.2      -- Медленное гипнотическое вращение
 --           }
 --       }
}

M.on_frame = function(self, dt, output_name)
    if not self.state.init then
        apply_palette()
        self.state.init = true
    end

    self.state.time = self.state.time + dt

    -- Аудио-реакция для сферы
    local bass = core.get_float("audio.bass", 0.0)
    core.get_layer(output_name, "sphere")
        :set("oscill_amp", 0.1 + (bass * 0.5))
        :set("wave_amp", bass * 0.2)
end

return M