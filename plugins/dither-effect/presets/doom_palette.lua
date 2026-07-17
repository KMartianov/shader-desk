-- Doom Retro Palette
return {
    shader_theme = "dither",
    downsample_scale = 3.0,
    dither_spread = 0.8,
    bayer_size = 1, -- 4x4 matrix
    colors_count = 11, -- Указываем точное количество цветов
    
    -- Этот блок кода выполнится ровно один раз при загрузке слоя!
    on_init = function()
        -- Задаем палитру (до 16 цветов). От самого темного к самому светлому.
        local hex_palette = {
            "#000000", "#281A1A", "#422824", "#5F3A30", 
            "#7D4C3A", "#9B634A", "#B67B59", "#CE9569", 
            "#E2AF7C", "#F0CD95", "#FAEAB6"
        }
        
        -- Конвертируем HEX в плоский массив Float {R, G, B, R, G, B...}
        local flat_rgb = {}
        for i, hex in ipairs(hex_palette) do
            local r = tonumber(hex:sub(2, 3), 16) / 255.0
            local g = tonumber(hex:sub(4, 5), 16) / 255.0
            local b = tonumber(hex:sub(6, 7), 16) / 255.0
            table.insert(flat_rgb, r)
            table.insert(flat_rgb, g)
            table.insert(flat_rgb, b)
        end
        
        -- Мгновенно отправляем массив в шину памяти C++
        core.set_float_array("dither.palette", flat_rgb)
    end
}