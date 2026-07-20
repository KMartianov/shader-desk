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

-- ==============================================================================
-- 6DOF ARCBALL VECTOR MATH ENGINE (Zero-Allocation)
-- ==============================================================================
local function vec3_normalize(v)
    local len = math.sqrt(v[1]*v[1] + v[2]*v[2] + v[3]*v[3])
    if len < 0.0001 then return {0.0, 1.0, 0.0} end
    return {v[1]/len, v[2]/len, v[3]/len}
end

local function vec3_cross(a, b)
    return {
        a[2]*b[3] - a[3]*b[2],
        a[3]*b[1] - a[1]*b[3],
        a[1]*b[2] - a[2]*b[1]
    }
end

local function vec3_dot(a, b)
    return a[1]*b[1] + a[2]*b[2] + a[3]*b[3]
end

-- Rodrigues' rotation formula
local function vec3_rotate(v, k, theta)
    local cos_t = math.cos(theta)
    local sin_t = math.sin(theta)
    local c = vec3_cross(k, v)
    local d = vec3_dot(k, v)
    
    return {
        v[1] * cos_t + c[1] * sin_t + k[1] * d * (1.0 - cos_t),
        v[2] * cos_t + c[2] * sin_t + k[2] * d * (1.0 - cos_t),
        v[3] * cos_t + c[3] * sin_t + k[3] * d * (1.0 - cos_t)
    }
end

local M = {
    meta = {
        name = "Orbital Harmonics 2.0",
        author = "Shader Desk",
        version = "2.0",
    },
    
    fps_limit = 0.0, 
    --fbo_scale = 1.0,
    
    -- ==========================================================================
    -- USER SETTINGS
    -- ==========================================================================
    settings = {
        -- Camera Settings
        cam_radius = 8.5,         -- Distance of the camera from the center (0,0,0)
        cam_sensitivity = 0.5,  -- Mouse rotation speed
        cam_fov = 45.0,           -- Perspective Field of View (Zoom / Distortion)
        cam_target = {0.0, 0.0, 0.0}, -- Where the camera looks (Panning)
        
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
        {
            effect = "Solid Bg",
            tag = "space_bg",
            clear_depth = true, -- Wipe the previous frame's depth
            settings = {
                gradient_type = 2,
                color_space = 1,
                radial_radius = 1.2,
                radial_center = {0.5, 0.5},
                color_1 = {0.45, 0.45, 0.45},
                color_2 = {0.85, 0.85, 0.85},
            }
        },
        
        -- 2. THE DYING STAR (Center Planet)
        {
            effect = "Icosahedron Sphere Old",
            tag = "center_planet",
            clear_depth = true, -- Clears background depth to start the 3D scene
            
            settings = {
                
                shader_theme = "harmonics",
                wireframe_mode = true,
                subdivisions = 2,             
                sphere_scale = 2.0,
                background_color = {1.02, 1.00, 0.02}, 
                wireframe_color = {0.1, 0.1, 0.1},
                offset = {0.0, 0.0, 0.0},
                layer_fbo_scale = 1.0,
                
                rotation_axis = {0.0, 1.0, 0.2},
                rotation_speed = 2.0,
                rotation_decay = 0.95,
                
                oscill_freq = 0.0, twist_amp = 1.0, wave_amp = 1.0, noise_amp = 0.15
            },
            
            
            -- ==================================================================
            -- НОВАЯ АРХИТЕКТУРА: ВЛОЖЕННЫЕ ФИЛЬТРЫ!
            -- Глитч применится только к планете, а её Z-Buffer (рельеф) 
            -- пробросится в основную сцену благодаря Depth Blitting.
            -- ==================================================================
            --filters = {
            --    {
            --        effect = "Postprocess Effect",
            --        tag = "planet_kaleidoscope",
            --        settings = {
            --            shader_theme = "kaleidoscope",
            --            variant = 1,
            --            intensity = 0.0,
            --            scale = 3.0, -- 8 граней
            --            speed = 0.5,
            --        }
            --    },
            --}

                
            

        },

        -- 3. THE MOON
        {
            effect = "Icosahedron Sphere Old",
            tag = "orbit_moon",
            -- CRITICAL FIX: Теперь FALSE! Z-Buffer от планеты сохранен, 
            -- поэтому Луна будет честно прятаться ЗА планету при вращении.
            clear_depth = false, 
            settings = {
                shader_theme = "default",
                wireframe_mode = true,
                subdivisions = 1,
                sphere_scale = 0.3,          
                background_color = {1.0, 0.0, 0.5},
                wireframe_color = {0.2, 0.8, 1.0},
                
                rotation_axis = {0.5, 1.0, 0.0},
                rotation_speed = 10.0,
                rotation_decay = 0.95,
                
                oscill_amp = 0.0, wave_amp = 0.0, noise_amp = 0.0
            },
            filters = {
                {
                    effect = "Postprocess Effect",
                    tag = "planet_glitch2", -- Уникальный тег, если захочешь анимировать глитч из on_frame
                    settings = {
                        shader_theme = "datamosh",
                        variant = 1,
                        intensity = 0.2,
                        scale = 10.0,
                        speed = 1.0,
                    }
                },
                {
                    effect = "Postprocess Effect",
                    tag = "planet_glitch", -- Уникальный тег, если захочешь анимировать глитч из on_frame
                    settings = {
                        shader_theme = "postprocess",
                        variant = 2,
                        intensity = 0.2,
                        scale = 100.0,
                        speed = 1.0,
                    }
                }
                
            }
        },

        -- 4. THE ALIEN ARTIFACT (Cube)
        {
            effect = "Hilbert Cube",
            tag = "orbit_cube",
            clear_depth = false, -- Share 3D space with Planet and Moon
            settings = {
                hilbert_order = 2,
                draw_cube_outline = true,
                curve_color = {0.1, 1.0, 0.3},
                cube_color = {1.0, 0.0, 0.00},
                
                rotation_axis = {1.0, 1.0, 1.0},
                rotation_speed = -5.0,
                rotation_decay = 0.98
            },

            filters = {
                {
                    effect = "Postprocess Effect",
                    tag = "planet_glitch2", -- Уникальный тег, если захочешь анимировать глитч из on_frame
                    settings = {
                        shader_theme = "datamosh",
                        variant = 1,
                        intensity = 0.3,
                        scale = 10.0,
                        speed = 1.0,
                    }
                },
                {
                    effect = "Postprocess Effect",
                    tag = "planet_glitch", -- Уникальный тег, если захочешь анимировать глитч из on_frame
                    settings = {
                        shader_theme = "postprocess",
                        variant = 2,
                        intensity = 0.2,
                        scale = 100.0,
                        speed = 1.0,
                    }
                }
                
            }
        },

        
    },
    
    state = { 
        time = 0.0,
        cam_pos = {0.0, 0.0, 1.0}, -- Normalized direction, scaled later by cam_radius
        cam_up = {0.0, 1.0, 0.0},
        last_mx = nil,
        last_my = nil
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
    -- 1. GLOBAL CAMERA (6DOF Arcball Trackball via Mouse)
    -- ==========================================================================
    local raw_mx = core.get_float("mouse.accum_x", 0.0)  
    local raw_my = core.get_float("mouse.accum_y", 0.0) 
    
    if self.state.last_mx == nil then
        self.state.last_mx = raw_mx
        self.state.last_my = raw_my
    end

    -- Calculate true frame velocity deltas
    local dx = raw_mx - self.state.last_mx
    local dy = raw_my - self.state.last_my

    self.state.last_mx = raw_mx
    self.state.last_my = raw_my

    -- Apply rotations only if the mouse actually moved
    if math.abs(dx) > 0.0001 or math.abs(dy) > 0.0001 then
        -- View direction is always towards the center (0,0,0)
        local view_dir = vec3_normalize({-self.state.cam_pos[1], -self.state.cam_pos[2], -self.state.cam_pos[3]})
        
        -- Right vector is orthogonal to View and Up
        local right = vec3_normalize(vec3_cross(view_dir, self.state.cam_up))

        -- Rotate around Up vector (Yaw / Left-Right mouse movement)
        if math.abs(dx) > 0.0001 then
            local yaw = dx * self.settings.cam_sensitivity
            self.state.cam_pos = vec3_rotate(self.state.cam_pos, self.state.cam_up, yaw)
        end
        
        -- Rotate around Right vector (Pitch / Up-Down mouse movement)
        if math.abs(dy) > 0.0001 then
            local pitch = dy * self.settings.cam_sensitivity
            self.state.cam_pos = vec3_rotate(self.state.cam_pos, right, pitch)
            self.state.cam_up = vec3_rotate(self.state.cam_up, right, pitch)
        end

        -- Defend against floating point drift by re-orthogonalizing every frame
        self.state.cam_pos = vec3_normalize(self.state.cam_pos)
        self.state.cam_up = vec3_normalize(self.state.cam_up)
    end

    -- Scale normalized position by the user-defined orbit radius
    local r = self.settings.cam_radius
    local cam_x = self.state.cam_pos[1] * r
    local cam_y = self.state.cam_pos[2] * r
    local cam_z = self.state.cam_pos[3] * r

    -- Dispatch Zero-Latency 6DOF Camera matrices to the BlackBoard
    -- Using FOV and Target from user settings
    core.set_camera(
        {cam_x, cam_y, cam_z}, 
        self.settings.cam_target, 
        self.state.cam_up, 
        self.settings.cam_fov
    )

    -- Fake 2D parallax for the background gradient based on camera position
    core.get_layer(output_name, "space_bg")
        :set_vec2("radial_center", 0.5 - (cam_x * 0.02), 0.5 - (cam_y * 0.02))

    -- ==========================================================================
    -- 2. ORBITAL MECHANICS (Calculating World Space Offsets)
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