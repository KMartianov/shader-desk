-- ==============================================================================
-- MAIN ROUTER CONFIGURATION
-- This file acts as the primary hardware-to-scene router. It assigns specific 
-- visual scenes to physical Wayland outputs and initializes global data providers.
-- ==============================================================================

_G.ctl = require("ctl")
core = core or {}

-- ==============================================================================
-- 1. HARDWARE ROUTING
-- Map physical monitors (e.g., eDP-1, DP-1) to specific scenes.
-- The "*" acts as a fallback for any unmapped connected displays.
-- ==============================================================================
core.outputs = {
    --["*"] = core.load_scene("calm_floating_gallery")
    --["*"] = core.load_scene("cyber_horizon")
    --["*"] = core.load_scene("macos_mesh")
    --["*"] = core.load_scene("macos_mesh")
    --["*"] = core.load_scene("live_stream")
    --["*"] = core.load_scene("glitch_demo")
    ["*"] = core.load_scene("calm_floating_gallery")
    
    
    
    
    -- Examples of explicit hardware targeting:
    -- ["HDMI-A-1"] = core.load_scene("default"),
    -- ["DP-2"]     = core.load_scene("liquid")
}

-- ==============================================================================
-- 2. GLOBAL ANIMATION HOOK
-- Wayland compositor invokes this function at the refresh rate of the monitor 
-- (e.g., 60Hz or 144Hz). It dynamically resolves the assigned scene for the 
-- current output and delegates the execution to the scene's internal state.
-- ==============================================================================
core.on_frame(function(dt, output_name)
    -- Clamp delta time to prevent massive jumps after waking from suspend
    if dt > 0.1 then dt = 0.016 end 
    
    local out_conf = core.outputs[output_name] or core.outputs["*"]
    
    -- Delegate execution to the scene's isolated tick function
    if out_conf and type(out_conf.on_frame) == "function" then
        out_conf.on_frame(out_conf, dt, output_name)
    end
end)

-- ==============================================================================
-- 3. DATA PROVIDERS
-- Configuration for standalone C++ daemons streaming data into the BlackBoard.
-- ==============================================================================
core.providers = {
    ["Evdev Pointer Provider"] = {
        enabled = true,
        mouse_sensitivity = 10.0,
        touchpad_sensitivity = 2.5,
    },
    ["Native FFTW Audio Provider"] = {
        enabled = true,
        smoothing = 0.85,          
        volume_multiplier = 100.0,
        bass_multiplier = 1.5,     
        treble_multiplier = 2.0
    }
}