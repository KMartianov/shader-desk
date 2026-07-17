local M = {
    meta = { name = "GameBoy Dither & High-Res Cube" },
    state = { init = false }
}

-- Хелпер для конвертации HEX в BlackBoard массив
local function apply_palette()
    local hex_colors = {"#0f380f", "#306230", "#8bac0f", "#9bbc0f"}
    local flat_rgb = {}
    for _, hex in ipairs(hex_colors) do
        table.insert(flat_rgb, tonumber(hex:sub(2, 3), 16) / 255.0)
        table.insert(flat_rgb, tonumber(hex:sub(4, 5), 16) / 255.0)
        table.insert(flat_rgb, tonumber(hex:sub(6, 7), 16) / 255.0)
    end
    core.set_float_array("dither.palette", flat_rgb)
end

M.layers = {
    -- 1. БАЗОВЫЙ СЛОЙ (Картинка)
    {
        effect = "Image Bg",
        tag = "bg_image",
        settings = {
            base_image_path = "img/img1.png",
            fill_mode = 0, -- Cover
            scale = 1.05   -- Чуть больше для параллакса
        }
    },
    
    -- 2. ПОСТ-ОБРАБОТКА (Применяется только к картинке!)
    {
        effect = "Dither Effect",
        tag = "retro_dither",
        postprocess = true, 
        settings = {
            downsample_scale = 4.0, -- Крупные квадратные пиксели
            dither_spread = 0.6,    -- Интенсивность шума
            bayer_size = 1,         -- 4x4 матрица
            colors_count = 4        -- Используем все 4 цвета
        }
    },

    -- 3. ПЕРЕДНИЙ ПЛАН (Рисуется четко, БЕЗ дизеринга)
    {
        effect = "Hilbert Cube",
        tag = "crisp_cube",
        postprocess = false,
        settings = {
            hilbert_order = 3,
            draw_cube_outline = true,
            curve_color = {0.607, 0.737, 0.058}, -- Цвет #9bbc0f (светло-зеленый)
            cube_color = {0.058, 0.219, 0.058},  -- Цвет #0f380f (темно-зеленый)
            offset = {0.0, 0.0, 0.0}
        }
    }
}

M.on_frame = function(self, dt, output_name)
    -- Загружаем палитру в видеокарту только один раз при старте
    if not self.state.init then
        apply_palette()
        self.state.init = true
    end

    -- Параллакс для фона
    local mx = core.get_float("mouse.accum_x", 0.0)
    local my = core.get_float("mouse.accum_y", 0.0)
    
    core.get_layer(output_name, "bg_image"):set_vec2("offset", mx * 0.01, my * 0.01)
end

return M