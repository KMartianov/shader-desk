-- ==============================================================================
-- SCENE: Orbital Harmonics (Kinematics & Global Camera Showcase)
-- ==============================================================================
-- Architecture Highlights:
-- 1. Global Camera: Controlled via Lua using spherical coordinates (Mouse X/Y).
-- 2. Shared Z-Buffer: `clear_depth = false` allows independent .so plugins 
--    (Planet, Moon, Cube) to correctly occlude each other in true 3D space.
-- 3. Kinematics: Objects rotate automatically via C++ physics. Bass impacts 
--    send an "impulse" to the planet's rotation_speed, decaying naturally.
-- ==============================================================================

local M = {
    meta = {
        name = "Orbital Harmonics 2.0",
        author = "Shader Desk",
        version = "2.0",
    },
    
    fps_limit = 0.0, 
    
    -- ==========================================================================
    -- USER SETTINGS
    -- ==========================================================================
    settings = {
        -- Camera Settings
        cam_radius = 5.5,         -- Distance of the camera from the center (0,0,0)
        cam_sensitivity = 0.003,  -- Mouse rotation speed
        
        -- Orbital Mechanics
        orbit_speed_moon = 0.8,
        orbit_speed_cube = 0.5,
        
        -- Aesthetics
        planet_color = {1.0, 0.2, 0.5}, -- Hot Pink / Magenta
        moon_color   = {0.2, 0.8, 1.0}, -- Cyan
        cube_color   = {0.1, 1.0, 0.3}, -- Neon Green
    },

    -- ==========================================================================
    -- RENDER PIPELINE
    -- ==========================================================================
    layers = {
        -- 1. DEEP SPACE BACKGROUND (2D)
        --{
        --    effect = "Solid Bg",
        --    tag = "space_bg",
        --    clear_depth = true, -- Wipe the previous frame's depth
        --    settings = {
        --        gradient_type = 2,       -- Radial Offset
        --        color_space = 1,         -- OKLab for perfect blending
        --        radial_radius = 1.2,
        --        radial_center = {0.5, 0.5},
        --        color_1 = {0.05, 0.01, 0.08}, -- Dark Purple Core
        --        color_2 = {0.00, 0.00, 0.00}, -- Pitch Black Edges
        --    }
        --},

        -- 2. THE DYING STAR (Center Planet)
        {
            effect = "Icosahedron Sphere Old",
            tag = "center_planet",
            clear_depth = true, -- Clears background depth to start the 3D scene
            settings = {
                shader_theme = "harmonics",
                wireframe_mode = false,
                subdivisions = 6,             
                sphere_scale = 1.2,
                background_color = {1.02, 1.00, 0.02}, 
                wireframe_color = {2.0, 0.2, 0.5},
                offset = {0.0, 0.0, 0.0},
                
                -- KINEMATICS (Base Physics)
                rotation_axis = {0.0, 1.0, 0.2}, -- Tilted Y axis
                rotation_speed = 2.0,            -- Base continuous rotation force
                rotation_decay = 0.95,           -- Friction (used when impulses hit)
                
                -- Visual Audio Reactivity (Handled in Shader)
                oscill_freq = 0.5,
                twist_amp = 0.3,              
                wave_amp = 0.1,
                noise_amp = 0.15
            }
        },

        --{
        --     effect = "Postprocess Effect", -- Накидываем сверху глитч
        --     tag = "glitch_filter",
        --     postprocess = true,         -- КРИТИЧЕСКИ ВАЖНО! Включит Ping-Pong FBO
        --     settings = {
        --        shader_theme = "datamosh",
        --        variant = 1,     -- Режим блочного датамоша
        --        intensity = 0.1, -- Чем выше, тем больше экран разваливается на квадраты
        --        scale = 100.0,    -- Плотность макро-блоков (размер квадратов)
        --        speed = 2.0,     -- Частота "поломки" (зависит от BPM, если захочешь привязать к музыке)
        --     }
        -- },
--
        -- {
        --     effect = "Postprocess Effect", -- Накидываем сверху глитч
        --     tag = "glitch_filter",
        --     postprocess = true,         -- КРИТИЧЕСКИ ВАЖНО! Включит Ping-Pong FBO
        --     settings = {
        --        shader_theme = "postprocess",
        --        variant = 2,     -- Режим блочного датамоша
        --        intensity = 0.5,-- Чем выше, тем больше экран разваливается на квадраты
        --        scale = 100.0,    -- Плотность макро-блоков (размер квадратов)
        --        speed = 5.0,     -- Частота "поломки" (зависит от BPM, если захочешь привязать к музыке)
        --     }
        -- },

        -- 3. THE MOON
        {
            effect = "Icosahedron Sphere Old",
            tag = "orbit_moon",
            -- CRITICAL: False! This allows the moon to physically go behind the planet!
            clear_depth = true, 
            settings = {
                shader_theme = "default",
                wireframe_mode = true,
                subdivisions = 1,             -- Low poly
                sphere_scale = 0.3,          
                background_color = {1.0, 0.0, 0.5},
                wireframe_color = {0.2, 0.8, 1.0},
                
                -- KINEMATICS: Fast self-spin
                rotation_axis = {0.5, 1.0, 0.0},
                rotation_speed = 10.0,
                rotation_decay = 0.95,
                
                oscill_amp = 0.0, wave_amp = 0.0, noise_amp = 0.0
            }
        },
--
      --  -- 4. THE ALIEN ARTIFACT (Cube)
      --  {
      --      effect = "Hilbert Cube",
      --      tag = "orbit_cube",
      --      clear_depth = false, -- Share 3D space with Planet and Moon
      --      settings = {
      --          hilbert_order = 3,
      --          draw_cube_outline = true,
      --          curve_color = {0.1, 1.0, 0.3},
      --          cube_color = {1.0, 0.0, 0.00},
      --          
      --          -- KINEMATICS: Chaotic self-spin
      --          rotation_axis = {1.0, 1.0, 1.0},
      --          rotation_speed = -5.0,
      --          rotation_decay = 0.98
      --      }
      --  },

        
    },
    
    state = { 
        time = 0.0,
        cam_phi = 0.0, 
        cam_theta = 0.0 
    }
}

-- ==============================================================================
-- CONTROL PLANE (LUA MATH)
-- Runs at monitor refresh rate. 
-- Computes orbits, camera spherical coordinates, and audio physics impulses.
-- ==============================================================================
M.on_frame = function(self, dt, output_name)
    self.state.time = self.state.time + dt
    local t = self.state.time

    -- ==========================================================================
    -- 1. GLOBAL CAMERA (Spherical Orbit via Mouse)
    -- ==========================================================================
    -- Read infinite accumulated mouse deltas from the Wayland Evdev Daemon
    local raw_mx = core.get_float("mouse.accum_x", 0.0) * 100.0
    local raw_my = core.get_float("mouse.accum_y", 0.0) * 100.0
    
    -- Smoothly interpolate current angles toward target angles (Cinematic drag)
    local target_phi = raw_mx * self.settings.cam_sensitivity
    local target_theta = raw_my * self.settings.cam_sensitivity
    
    -- Restrict vertical angle (Theta) to avoid gimbal lock (flipping upside down)
    target_theta = math.max(-1.4, math.min(1.4, target_theta))

    self.state.cam_phi = self.state.cam_phi + (target_phi - self.state.cam_phi) * 5.0 * dt
    self.state.cam_theta = self.state.cam_theta + (target_theta - self.state.cam_theta) * 5.0 * dt

    -- Convert Spherical (Radius, Theta, Phi) to Cartesian (X, Y, Z)
    local r = self.settings.cam_radius
    local cam_x = r * math.cos(self.state.cam_theta) * math.sin(self.state.cam_phi)
    local cam_y = r * math.sin(self.state.cam_theta)
    local cam_z = r * math.cos(self.state.cam_theta) * math.cos(self.state.cam_phi)

    -- Dispatch Zero-Latency Camera matrices to the BlackBoard
    -- All Kinematic plugins will automatically read this!
    core.set_camera({cam_x, cam_y, cam_z}, {0.0, 0.0, 0.0}, 45.0)

    -- Fake 2D parallax for the background gradient based on camera position
    core.get_layer(output_name, "space_bg")
        :set_vec2("radial_center", 0.5 - (cam_x * 0.02), 0.5 - (cam_y * 0.02))

    -- ==========================================================================
    -- 2. PHYSICS IMPULSES (Audio Reactivity)
    -- ==========================================================================
    local bass = core.get_float("audio.bass", 0.0)
    
    -- The C++ Kinematic engine constantly degrades speed back to 0 using `rotation_decay`.
    -- If we add a base force + a massive spike on beat, the planet will "kick" and 
    -- smoothly slow down to its base rotational speed.
    local base_force = 2.0
    local impulse = 0.0
    if bass > 0.6 then
        impulse = bass * 40.0 -- Explosive rotational force
    end
    
    core.get_layer(output_name, "center_planet")
        :set("rotation_speed", base_force + impulse)

    -- ==========================================================================
    -- 3. ORBITAL MECHANICS (Calculating World Space Offsets)
    -- ==========================================================================
    -- Moon Orbit (Horizontalish plane)
    local m_r = 2.8
    local m_speed = self.settings.orbit_speed_moon
    local mx = math.cos(t * m_speed) * m_r
    local mz = math.sin(t * m_speed) * m_r
    local my = math.sin(t * m_speed * 0.5) * 0.5 -- Slight vertical bob
    
    core.get_layer(output_name, "orbit_moon")
        :set_vec3("offset", mx, my, mz)

    -- Cube Orbit (Verticalish plane, larger radius)
    local c_r = 3.8
    local c_speed = self.settings.orbit_speed_cube
    local cx = math.sin(t * c_speed) * c_r
    local cy = math.cos(t * c_speed) * c_r
    local cz = math.sin(t * c_speed * 0.3) * 1.5 -- Depth variation
    
    core.get_layer(output_name, "orbit_cube")
        :set_vec3("offset", cx, cy, cz)
end

return M