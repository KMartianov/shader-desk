-- ==============================================================================
-- SCENE: Orbital Harmonics (Kinematics & Global Camera Showcase)
-- ==============================================================================
-- Architecture Highlights:
-- 1. Global Camera: Free-look, quaternion-driven orbit (Mouse X/Y). No Euler
--    angles are stored anywhere, so there is no pole to lock at -- the camera
--    can pitch straight through vertical and loop all the way around ("кувырок").
-- 2. Shared Z-Buffer: `clear_depth = false` allows independent .so plugins 
--    (Planet, Moon, Cube) to correctly occlude each other in true 3D space.
-- 3. Kinematics: Objects rotate automatically via C++ physics. Bass impacts 
--    send an "impulse" to the planet's rotation_speed, decaying naturally.
-- ==============================================================================

-- ==============================================================================
-- QUATERNION HELPERS
-- Lua has no glm, so this is a tiny, self-contained quaternion library used
-- only by the free-look camera below. Quaternions are plain {w, x, y, z} tables.
-- Composing rotations this way (instead of storing/accumulating yaw+pitch as
-- Euler angles) is what actually fixes both camera complaints at once:
--  - there is no gimbal-lock pole, so pitch can go all the way through
--    vertical and the camera can do a full loop/flip,
--  - the "up" vector is derived from the same orientation as the view
--    direction, so it can never end up parallel to it (which is the usual
--    cause of a lookAt() camera appearing to "swap" left and right).
-- ==============================================================================
local function quat_mul(a, b)
    -- Hamilton product a*b: rotate by b first, then by a.
    local aw, ax, ay, az = a[1], a[2], a[3], a[4]
    local bw, bx, by, bz = b[1], b[2], b[3], b[4]
    return {
        aw*bw - ax*bx - ay*by - az*bz,
        aw*bx + ax*bw + ay*bz - az*by,
        aw*by - ax*bz + ay*bw + az*bx,
        aw*bz + ax*by - ay*bx + az*bw,
    }
end

local function quat_normalize(q)
    local len = math.sqrt(q[1]*q[1] + q[2]*q[2] + q[3]*q[3] + q[4]*q[4])
    if len < 1e-8 then return {1.0, 0.0, 0.0, 0.0} end
    return {q[1]/len, q[2]/len, q[3]/len, q[4]/len}
end

local function quat_from_axis_angle(ax, ay, az, angle)
    local half = angle * 0.5
    local s = math.sin(half)
    return {math.cos(half), ax*s, ay*s, az*s}
end

-- Rotates vector (vx,vy,vz) by unit quaternion q. Optimized form of
-- v' = q * v * conj(q) that avoids building a full matrix.
local function quat_rotate_vec(q, vx, vy, vz)
    local qw, qx, qy, qz = q[1], q[2], q[3], q[4]
    local tx = 2 * (qy*vz - qz*vy)
    local ty = 2 * (qz*vx - qx*vz)
    local tz = 2 * (qx*vy - qy*vx)
    return vx + qw*tx + (qy*tz - qz*ty),
           vy + qw*ty + (qz*tx - qx*tz),
           vz + qw*tz + (qx*ty - qy*tx)
end

local M = {
    meta = {
        name = "Orbital Harmonics 2.0",
        author = "Shader Desk",
        version = "2.0",
    },
    
    fps_limit = 0.0, 
    fbo_scale = 1.0,
    
    -- ==========================================================================
    -- USER SETTINGS
    -- ==========================================================================
    settings = {
        -- Camera Settings
        cam_radius = 8.5,             -- Distance of the camera from the center (0,0,0)
        cam_sensitivity = 0.0025,     -- Radians of rotation per raw mouse-delta unit. Tune to taste.
        cam_smoothing = 8.0,          -- Higher = snappier / less inertia, lower = floatier drag
        cam_invert_x = false,         -- Flip yaw direction if it feels backwards
        cam_invert_y = false,         -- Flip pitch direction if it feels backwards
        cam_max_step = 250.0,         -- Per-frame mouse-delta clamp (glitch/reconnect guard)
        
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
                color_1 = {0.05, 0.01, 0.08},
                color_2 = {0.00, 0.00, 0.00},
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
                subdivisions = 6,             
                sphere_scale = 2.0,
                background_color = {1.02, 1.00, 0.02}, 
                wireframe_color = {0.6, 0.2, 1.0},
                offset = {0.0, 0.0, 0.0},
                
                rotation_axis = {0.0, 1.0, 0.2},
                rotation_speed = 0.0,
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
            --    }
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

        -- Camera orientation as a single accumulated quaternion (identity =
        -- looking down -Z from (0,0,cam_radius)). Never rebuilt from angles,
        -- so it can spin through any axis indefinitely without a pole.
        cam_orientation = {1.0, 0.0, 0.0, 0.0},
        cam_vel_yaw = 0.0,
        cam_vel_pitch = 0.0,

        -- Reference sample of the (infinitely-accumulating) mouse counters,
        -- used to derive a small per-frame delta instead of an absolute
        -- position. See the on_frame comment below for why this matters.
        cam_has_mouse_ref = false,
        cam_last_raw_mx = 0.0,
        cam_last_raw_my = 0.0,
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
    -- 1. GLOBAL CAMERA (Free-look Orbit via Mouse, quaternion-driven)
    -- ==========================================================================
    -- The Evdev daemon's mouse.accum_x/accum_y counters accumulate forever for
    -- as long as the wallpaper process lives -- they are never reset. Reading
    -- them as an ABSOLUTE position (the old approach: angle = raw * sensitivity)
    -- means the resulting angle rides on top of a float32 value that only ever
    -- grows. Past about 2^24 (~16.7M) a float32 can no longer represent single
    -- pixels of motion precisely, so small mouse moves start rounding to zero
    -- or jumping in coarse steps -- which is exactly what shows up as the
    -- camera "periodically confusing" left and right after the wallpaper has
    -- been running a while.
    --
    -- The fix is to only ever look at the DIFFERENCE between this frame's and
    -- last frame's sample (a tiny, well-behaved number) and feed that into a
    -- persistent orientation we keep entirely on the Lua side (Lua numbers are
    -- doubles, so this state itself never runs into the same precision wall).
    local raw_mx = core.get_float("mouse.accum_x", 0.0)
    local raw_my = core.get_float("mouse.accum_y", 0.0)

    local dx, dy = 0.0, 0.0
    if self.state.cam_has_mouse_ref then
        dx = raw_mx - self.state.cam_last_raw_mx
        dy = raw_my - self.state.cam_last_raw_my
    end
    self.state.cam_last_raw_mx = raw_mx
    self.state.cam_last_raw_my = raw_my
    self.state.cam_has_mouse_ref = true

    -- Absorb any single-frame glitch (device reconnects, a stale precision
    -- jump, first-frame after hot-reload, etc.) instead of letting one bad
    -- sample snap the camera.
    local max_step = self.settings.cam_max_step
    dx = math.max(-max_step, math.min(max_step, dx))
    dy = math.max(-max_step, math.min(max_step, dy))

    if self.settings.cam_invert_x then dx = -dx end
    if self.settings.cam_invert_y then dy = -dy end

    -- This frame's raw rotation contribution, then smoothed into a velocity
    -- for the same "cinematic drag" feel the old lerp had -- but now it is
    -- smoothing a tiny incremental step, not chasing an ever-larger absolute
    -- target, so it can't accumulate precision error over time.
    local smooth_k = math.min(1.0, self.settings.cam_smoothing * dt)
    self.state.cam_vel_yaw   = self.state.cam_vel_yaw   + (dx * self.settings.cam_sensitivity - self.state.cam_vel_yaw)   * smooth_k
    self.state.cam_vel_pitch = self.state.cam_vel_pitch + (dy * self.settings.cam_sensitivity - self.state.cam_vel_pitch) * smooth_k

    -- Yaw turns around WORLD up; pitch turns around the camera's own current
    -- local right axis (read straight off the live orientation). Both are
    -- composed onto the persistent quaternion incrementally -- we never
    -- rebuild it from an absolute yaw/pitch pair, so there is no pole where
    -- left/right can flip and no limit stopping pitch from going all the way
    -- around into a full loop.
    local right_x, right_y, right_z = quat_rotate_vec(self.state.cam_orientation, 1.0, 0.0, 0.0)
    local yaw_q   = quat_from_axis_angle(0.0, 1.0, 0.0, self.state.cam_vel_yaw)
    local pitch_q = quat_from_axis_angle(right_x, right_y, right_z, -self.state.cam_vel_pitch)

    self.state.cam_orientation = quat_normalize(
        quat_mul(pitch_q, quat_mul(yaw_q, self.state.cam_orientation))
    )

    -- Derive the camera's position and up vector straight from the
    -- orientation quaternion -- up is always perpendicular to the view
    -- direction by construction, so it can never collapse into it the way a
    -- fixed world-up does near the poles.
    local r = self.settings.cam_radius
    local cam_x, cam_y, cam_z = quat_rotate_vec(self.state.cam_orientation, 0.0, 0.0, r)
    local up_x, up_y, up_z    = quat_rotate_vec(self.state.cam_orientation, 0.0, 1.0, 0.0)

    -- Dispatch Zero-Latency Camera matrices (+ up vector) to the BlackBoard
    -- All Kinematic plugins will automatically read this!
    core.set_camera({cam_x, cam_y, cam_z}, {0.0, 0.0, 0.0}, 45.0, {up_x, up_y, up_z})

    -- Fake 2D parallax for the background gradient based on camera position
    core.get_layer(output_name, "space_bg")
        :set_vec2("radial_center", 0.5 - (cam_x * 0.02), 0.5 - (cam_y * 0.02))

    -- ==========================================================================
    -- 2. PHYSICS IMPULSES (Audio Reactivity)
    -- ==========================================================================
    -- local bass = core.get_float("audio.bass", 0.0)
    
    -- The C++ Kinematic engine constantly degrades speed back to 0 using `rotation_decay`.
    -- If we add a base force + a massive spike on beat, the planet will "kick" and 
    -- smoothly slow down to its base rotational speed.
    -- local base_force = 2.0
    -- local impulse = 0.0
    -- if bass > 0.6 then
    --     impulse = bass * 40.0 -- Explosive rotational force
    -- end
    
    -- core.get_layer(output_name, "center_planet")
    --   :set("rotation_speed", base_force + impulse)

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