-- ==============================================================================
-- SCENE: Windows to Other Worlds
-- Demonstrates multi-layer compositing using ONLY the "Image Bg" plugin.
-- Showcases the new Zero-Allocation C++ API (set_vec2, set_vec3) for smooth 
-- 144Hz animations without triggering Lua Garbage Collection spikes.
--
-- IMPORTANT: For the "windows" effect to work, the images for Layer 2 and 3 
-- MUST be PNG files with transparent backgrounds (alpha channel).
-- ==============================================================================

local M = {
    meta = {
        name = "Windows to Other Worlds",
        author = "Shader Desk",
        version = "1.1",
    },
    
    fps_limit = 0.0, -- Unlocked FPS
    
    layers = {
        -- ==========================================================
        -- LAYER 1: The Void (Deep Background)
        -- ==========================================================
        {
            effect = "Image Bg",
            tag = "universe_bg",
            settings = {
                base_image_path = "img/img2.jpg", 
                fill_mode = 1,       -- Cover (Fills the entire screen)
                scale = 0.8,
                offset = {1.0, 0.0},
                rotation = 0.0,
                brightness = 1.0,    -- Darkened to highlight foreground
                contrast = 1.2,
                saturation = 2.0,    -- Desaturated
                tint_color = {0.05, 0.02, 0.15}, -- Deep purple void
                tint_intensity = 0.6,
                blur_radius = 1.0   -- Heavily blurred (Depth of Field)
            }
        },
        
        -- ==========================================================
        -- LAYER 2: The Left Portal (Floating Window)
        -- ==========================================================
        {
            effect = "Image Bg",
            tag = "portal_left",
            settings = {
                base_image_path = "img/img1.png", -- PNG with transparency!
                fill_mode = 1,       -- Tile (Prevents black bounds clipping)
                scale = 0.5,         -- Zoomed in
                offset = {-1.0, 0.4},-- Shifted to the left side of the screen
                rotation = 0.0,     -- Slightly tilted
                brightness = 1.1,
                contrast = 1.0,
                saturation = 2.0,    -- Vibrant colors
                tint_color = {1.0, 1.0, 1.0}, -- Warm orange tint
                tint_intensity = 0.0,
                blur_radius = 2.0    -- Slight blur
            }
        },

        -- ==========================================================
        -- LAYER 3: The Right Portal (Foreground Window)
        -- ==========================================================
        {
            effect = "Image Bg",
            tag = "portal_right",
            settings = {
                base_image_path = "img/img3.png", -- PNG with transparency!
                fill_mode = 1,       -- Tile
                scale = 0.4,         -- Closer to camera
                offset = {-1.0, -0.5}, -- Shifted to the right and slightly up
                rotation = 1.0,
                brightness = 1.0,    -- Brightest layer
                contrast = 1.0,
                saturation = 2.0,
                tint_color = {0.0, 0.0, 0.0}, -- Cold cyan tint
                tint_intensity = 0.3,
                blur_radius = 0.0    -- Perfectly sharp
            }
        }
    },
    
    state = { 
        time = 0.0 
    }
}

-- ==============================================================================
-- ANIMATION HOOK (Runs every frame / e.g. 144 times per second)
-- ==============================================================================
M.on_frame = function(self, dt, output_name)
    self.state.time = self.state.time + dt
    local t = self.state.time

    -- Zero-Latency IPC Telemetry Read: Get mouse movements for Parallax effect
    local mx = core.get_float("mouse.accum_x", 0.0)
    local my = core.get_float("mouse.accum_y", 0.0)

    -- --------------------------------------------------------------------------
    -- 1. Animate Background (Slow panning and breathing blur)
    -- --------------------------------------------------------------------------
    local bg_pan_x = math.sin(t * 0.1) * 0.1
    local bg_pan_y = math.cos(t * 0.15) * 0.1
    local bg_blur = 10.0 + math.sin(t * 0.5) * 4.0
    
    --core.get_layer(output_name, "universe_bg")
    --    :set_vec2("offset", bg_pan_x, bg_pan_y) -- NEW: Zero-Allocation call!
     --   :set("blur_radius", bg_blur)

    -- --------------------------------------------------------------------------
    -- 2. Animate Left Portal (Parallax, Color Cycling, Scale Breathing)
    -- --------------------------------------------------------------------------
    local left_scale = (1.6 + math.sin(t * 1.2) * 0.05)/5
    
    -- Dynamic RGB calculation (Color cycling)
    local r = 0.5 + 0.5 * math.sin(t * 2.0)
    local g = 0.5 + 0.5 * math.sin(t * 2.0 + 2.094)
    local b = 0.5 + 0.5 * math.sin(t * 2.0 + 4.188)

    --core.get_layer(output_name, "portal_left")
        -- Mouse parallax (moves slightly with the mouse)
        --:set_vec2("offset", -0.3 + (mx * 0.015), my * 0.015) 
        --:set("scale", left_scale)
        --:set_vec3("tint_color", r, g, b)        -- NEW: Zero-Allocation call!

    -- --------------------------------------------------------------------------
    -- 3. Animate Right Portal (Rotation and Brightness Flickering)
    -- --------------------------------------------------------------------------
    local right_rot = 15.0 + math.sin(t * 0.8) * 8.0
    local right_bright = 1.1 + math.sin(t * 8.0) * 0.15 -- Fast neon flickering

    --core.get_layer(output_name, "portal_right")
        -- Stronger mouse parallax (foreground element moves faster)
        --:set_vec2("offset", 0.4 + (mx * 0.04), 0.1 + (my * 0.04)) 
        --:set("rotation", right_rot)
        --:set("brightness", right_bright)
end

return M
