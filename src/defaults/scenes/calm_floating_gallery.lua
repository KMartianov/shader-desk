-- ==============================================================================
-- SCENE: Calm Floating Gallery
-- ==============================================================================

local M = {
    meta = {
        name = "Calm Floating Gallery",
        author = "Shader Desk",
        version = "1.2",
    },
    
    fps_limit = 0.0, 
    
    layers = {
        -- ==========================================================
        -- LAYER 1: Фон (Растянут на весь экран, размыт и затемнен)
        -- ==========================================================
        {
            effect = "Image Bg",
            tag = "gallery_bg",
            settings = {
                -- Используем мрачную красную шапочку как атмосферный фон
                base_image_path = "img/img1.png", 
                fill_mode = 0,       -- 0 = Cover (Строго заполняет весь экран без черных рамок)
                scale = 1.1,         -- Чуть увеличен для параллакса
                offset = {0.0, 0.0},
                rotation = 0.0,
                
                -- Спокойные, нейтральные настройки цвета
                brightness = 0.25,   -- Сильно затемняем фон
                contrast = 1.0,
                saturation = 0.6,    -- Слегка обесцвечиваем
                tint_color = {0.1, 0.1, 0.15}, -- Легкий холодный оттенок тени
                tint_intensity = 0.5,
                blur_radius = 15.0   -- Сильное размытие фона
            }
        },
        
        -- ==========================================================
        -- LAYER 2: Девушка (Слева)
        -- ==========================================================
        {
            effect = "Image Bg",
            tag = "pic_girl",
            settings = {
                base_image_path = "img/img2.jpg", 
                fill_mode = 1,       -- 1 = Contain (Картинка в своих рамках)
                scale = 0.55,        -- Размер на экране
                offset = {-0.45, 0.1}, -- Сдвиг влево и чуть вверх
                rotation = 0.0,     -- Легкий наклон
                
                -- Дефолтные настройки цвета (без искажений)
                brightness = 1.0,
                contrast = 1.0,
                saturation = 1.0,    
                tint_color = {1.0, 1.0, 1.0},
                tint_intensity = 0.0,
                blur_radius = 0.0    -- Четкая картинка
            }
        },

        -- ==========================================================
        -- LAYER 3: Красная Шапочка (Справа)
        -- ==========================================================
        {
            effect = "Image Bg",
            tag = "pic_red_riding",
            settings = {
                base_image_path = "img/img1.png", 
                fill_mode = 1,       
                scale = 0.75,        -- Вертикальная картинка, делаем ее побольше
                offset = {0.5, 0.0}, -- Сдвиг вправо
                rotation = 0.0,
                
                brightness = 1.0,
                contrast = 1.0,
                saturation = 1.0,
                tint_color = {1.0, 1.0, 1.0}, 
                tint_intensity = 0.0,
                blur_radius = 0.0    
            }
        },

        -- ==========================================================
        -- LAYER 4: Котик (Внизу, ближе к центру)
        -- ==========================================================
        {
            effect = "Image Bg",
            tag = "pic_cat",
            settings = {
                base_image_path = "img/img3.png", 
                fill_mode = 1,       
                scale = 0.45,        -- Котик поменьше
                offset = {-0.1, -0.4}, -- Сдвиг вниз и чуть левее центра
                rotation = 0.0,
                
                brightness = 1.0,
                contrast = 1.0,
                saturation = 1.0,
                tint_color = {1.0, 1.0, 1.0}, 
                tint_intensity = 0.0,
                blur_radius = 0.0    
            }
        }
    },
    
    state = { 
        time = 0.0 
    }
}

-- ==============================================================================
-- ANIMATION HOOK (Плавное колыхание и легкий параллакс)
-- ==============================================================================
M.on_frame = function(self, dt, output_name)
    self.state.time = self.state.time + dt
    local t = self.state.time
    local mult = 10.0

    -- Считываем мышь для параллакса
    local mx = core.get_float("mouse.accum_x", 0.0)
    local my = core.get_float("mouse.accum_y", 0.0)

    -- 1. Анимация Фона (очень медленное движение в противовес мыши)
    local bg_x = math.sin(t * 0.05) * 0.02 - (mx * 0.005) * mult
    local bg_y = math.cos(t * 0.07) * 0.02 - (my * 0.005) * mult
    
    core.get_layer(output_name, "gallery_bg")
        :set_vec2("offset", bg_x, bg_y)

    -- 2. Девушка (колыхание + параллакс)
    local girl_base_x, girl_base_y = -0.15, 0.1
    local girl_x = girl_base_x + math.sin(t * 0.3) * 0.015 + (mx * 0.015) * mult
    local girl_y = girl_base_y + math.cos(t * 0.4) * 0.020 + (my * 0.015) * mult
    local girl_rot = -2.0 + math.sin(t * 0.2) * 1.5 -- Легкое покачивание угла

    core.get_layer(output_name, "pic_girl")
        :set_vec2("offset", girl_x, girl_y)
        --:set("rotation", girl_rot)

    -- 3. Красная шапочка
    local red_base_x, red_base_y = 0.5, 0.0
    local red_x = red_base_x + math.cos(t * 0.25) * 0.020 + (mx * 0.020) * mult
    local red_y = red_base_y + math.sin(t * 0.35) * 0.015 + (my * 0.020) * mult
    local red_rot = 3.0 + math.cos(t * 0.15) * 1.0

    core.get_layer(output_name, "pic_red_riding")
        :set_vec2("offset", red_x, red_y)
        --:set("rotation", red_rot)

    -- 4. Котик (котик "ближе" всего к экрану, поэтому двигается от мыши чуть сильнее)
    local cat_base_x, cat_base_y = -0.1, -0.4
    local cat_x = cat_base_x + math.sin(t * 0.4) * 0.025 + (mx * 0.030) * mult
    local cat_y = cat_base_y + math.sin(t * 0.5) * 0.025 + (my * 0.030) * mult
    local cat_rot = -4.0 + math.sin(t * 0.25) * 2.0

    core.get_layer(output_name, "pic_cat")
        :set_vec2("offset", cat_x, cat_y)
        --:set("rotation", cat_rot)
end

return M