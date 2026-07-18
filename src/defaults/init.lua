-- ==============================================================================
-- SHADER DESK: MAIN CONFIGURATION (init.lua)
-- ==============================================================================
-- This file acts as the primary hardware-to-scene router. It assigns specific 
-- visual scenes to physical Wayland outputs.
--
-- DIRECTORY STRUCTURE:
--  - /scenes/    : Lua scripts defining multi-layer compositions for screens.
--  - /plugins/   : Auto-generated parameter settings for individual .so plugins.
--  - /effects/   : User sandbox for modifying .glsl shaders (Hot-Reloadable).
--  - providers.lua : Contains OS daemons and Audio Equalizer settings.
-- ==============================================================================

-- 1. Initialize global dependencies
_G.core = _G.core or {}
_G.ctl = require("ctl")

-- 2. Load Data Providers and Equalizer logic from the external module
require("providers")

-- ==============================================================================
-- 3. HARDWARE ROUTING (Outputs to Scenes)
-- Map physical monitors (e.g., "eDP-1", "DP-1") to specific visual scenes.
-- The "*" key acts as a universal fallback for any unmapped connected displays.
-- ==============================================================================
core.outputs = {
    -- The core.load_scene() function searches for files in the /scenes/ folder.
    ["*"] = core.load_scene("orbital_harmonics"),

    -- Examples of explicit hardware targeting (Uncomment to use):
    -- ["eDP-1"]    = core.load_scene("macos_mesh"),
    -- ["HDMI-A-1"] = core.load_scene("liquid"),
    -- ["DP-2"]     = core.load_scene("cyber_horizon")
}

-- ==============================================================================
-- 4. GLOBAL ANIMATION HOOK
-- The Wayland compositor invokes this function at the exact refresh rate of the 
-- monitor (e.g., 60Hz, 144Hz, 240Hz). It delegates execution to the active scene.
-- ==============================================================================
core.on_frame(function(dt, output_name)
    -- Clamp extreme delta times to prevent physics explosions after system wake/suspend
    if dt > 0.1 then dt = 0.0166 end 
    
    local out_conf = core.outputs[output_name] or core.outputs["*"]
    
    -- Delegate execution to the scene's isolated `on_frame` function
    if out_conf and type(out_conf.on_frame) == "function" then
        out_conf.on_frame(out_conf, dt, output_name)
    end

    -- ==========================================================================
    -- EXPERIMENTAL: DYNAMIC SCRIPTING DEMO (Commented Out)
    -- ==========================================================================
    -- You can write custom, global visual logic here that applies to ALL screens.
    
    -- Example 1: Pulse the global wireframe color to the beat of the music.
    --[[
    local bass = core.get_float("audio.bass", 0.0)
    if bass > 0.7 then
        local pulse_intensity = (bass - 0.7) * 3.0 -- Scale 0.0 to ~1.0
        -- core.get_layer safely ignores the command if the target tag doesn't exist
        core.get_layer(output_name, "center_planet"):set_vec3("wireframe_color", 1.0, pulse_intensity, 0.5)
    else
        -- Return to a cool cyan color when resting
        core.get_layer(output_name, "center_planet"):set_vec3("wireframe_color", 0.2, 0.8, 1.0)
    end
    ]]--

    -- Example 2: Fake 2D Parallax effect using absolute mouse coordinates.
    --[[
    local abs_x = core.get_float("mouse.x", 0.5)
    local abs_y = core.get_float("mouse.y", 0.5)
    -- Move the background gradient center slightly based on mouse position
    core.get_layer(output_name, "space_bg"):set_vec2("radial_center", 0.5 + (abs_x - 0.5) * 0.1, 0.5 + (abs_y - 0.5) * 0.1)
    ]]--
end)