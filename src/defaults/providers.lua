-- ==============================================================================
-- SHADER DESK: DATA PROVIDERS CONFIGURATION (providers.lua)
-- ==============================================================================
-- This module isolates the configuration for background OS daemons (mouse, audio).
-- It is imported into init.lua. Saving this file triggers a Hot-Reload.
-- ==============================================================================

-- Ensure the global core object exists before we append to it
_G.core = _G.core or {}

-- ==============================================================================
-- 1. HARDWARE DAEMONS SETTINGS
-- ==============================================================================
core.providers = {
    ["Evdev Pointer Provider"] = {
        enabled = true,
        mouse_sensitivity = 1.0,     -- Multiplier for relative mouse movements
        touchpad_sensitivity = 3.0,  -- Multiplier for absolute touchpad coordinates
        invert_x = false,
        invert_y = false
    },
    ["Native FFTW Audio Provider"] = {
        enabled = true,
        smoothing = 0.85,            -- Audio decay inertia (0.0 = instant, 0.99 = smooth)
        volume_multiplier = 100.0,   -- Master gain for all audio reactivity
        bass_multiplier = 1.5,       -- Global bass boost (20Hz - 250Hz)
        mid_multiplier = 1.0,        -- Global mid boost (250Hz - 4000Hz)
        treble_multiplier = 2.0      -- Global treble boost (4000Hz - 20000Hz)
    }
}

-- ==============================================================================
-- 2. DYNAMIC EQUALIZER INTERPOLATION LOGIC
-- ==============================================================================
-- The C++ FFT daemon outputs exactly 64 frequency bands. 
-- Instead of manually writing 64 values, this helper function takes a small 
-- user-friendly array (e.g., 5 or 10 bands) and smoothly interpolates it 
-- into a 64-band mask using Linear Interpolation.
-- ==============================================================================
local function create_smooth_eq(simple_eq)
    local eq_64 = {}
    local num_points = #simple_eq
    
    -- Fallback for empty or single-value tables
    if num_points == 0 then
        for i = 1, 64 do eq_64[i] = 1.0 end
        return eq_64
    elseif num_points == 1 then
        for i = 1, 64 do eq_64[i] = simple_eq[1] end
        return eq_64
    end

    -- Linearly interpolate the simple array into exactly 64 steps
    for i = 1, 64 do
        local t = (i - 1) / (64 - 1) 
        local float_idx = 1 + t * (num_points - 1)
        
        local left_idx = math.floor(float_idx)
        local right_idx = math.ceil(float_idx)
        local fract = float_idx - left_idx
        
        if left_idx == right_idx then
            eq_64[i] = simple_eq[left_idx]
        else
            -- Lerp
            eq_64[i] = simple_eq[left_idx] * (1.0 - fract) + simple_eq[right_idx] * fract
        end
    end
    
    return eq_64
end

-- ==============================================================================
-- 3. EQUALIZER PRESETS
-- ==============================================================================

-- A 10-Band Graphic EQ setup
local eq_10_band_preset = {
    1.8,  -- Sub-Bass (Punch)
    1.8,  -- Bass
    1.0,  -- Low-Mid
    0.8,  -- Mid (Reduced mud)
    1.0,  -- High-Mid
    1.3,  -- Presence (Snares/Claps)
    1.1,  -- Brilliance
    0.8,  -- Highs
    0.5,  -- Air
    0.0   -- Extreme Highs (Cut white noise/hiss)
}

-- Inject the generated 64-band array directly into the C++ Microkernel's memory bus.
-- The Audio Provider C++ plugin will read this array in O(1) time every frame 
-- without any string parsing or performance overhead.
core.set_float_array("audio.eq_curve", create_smooth_eq(eq_10_band_preset))

-- Return the configuration (optional, but good practice for Lua modules)
return core.providers