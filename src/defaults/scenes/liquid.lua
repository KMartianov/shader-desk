-- ==============================================================================
-- SCENE: Liquid Topography
-- Demonstrates separation of concerns: Telemetry (BlackBoard) vs Control (Proxy)
-- ==============================================================================
local M = {}

M.layers = {
    {
        effect = "Gradient Bg",
        tag = "fluid_bg",
        settings = {
            blend_power = 1.5,
            bg_color = {1.0, 1.0, 1.0},
            enable_stripes = false,
            stripes_density = 150.0,
            stripes_opacity = 1.0,
            dithering_amount = 0.04
        }
    },
   -- {
   --     effect = "Hilbert Cube",
   --     tag = "wire_cube",
   --     settings = { 
   --         hilbert_order = 5, 
   --         draw_cube_outline = true, 
   --         rotation_decay = 0.98 
   --     }
   -- }
}

M.on_frame = function(self, dt, output_name)
    local t = core.time or 0
    core.time = t + dt

    -- Orbital dynamics for gradient control points
    local p1_x = 0.5 + math.sin(t * 0.8) * 0.4
    local p1_y = 0.5 + math.cos(t * 0.8) * 0.4
    
    local p2_x = 0.5 + math.sin(t * 0.5 + 3.14) * 0.3
    local p2_y = 0.5 + math.cos(t * 0.5 + 3.14) * 0.3

    local mx, my = 0.0, 0.0
    
    -- TELEMETRY READ: O(1) memory read from the Zero-Latency BlackBoard
    local bass = core.get_float("audio.bass", 0.0)

    -- TELEMETRY WRITE: Stream raw buffer data directly to the shader memory bus.
    -- This bypasses the plugin parameter system for maximum performance.
    core.set_float_array("grad.positions", { 
        --p1_x, p1_y, 
        0.5, 0.5

    })
    
    core.set_float_array("grad.colors", { 
        0.1, 0.2, 0.0,  
  
    })

    core.set_float_array("grad.radii", { 
        0.1, 
   
    })
    
    -- PLUGIN CONTROL: Use the Safe Proxy to update high-level effect configuration.
    -- Tell the C++ backend to actively process exactly 4 points in the shader loop.
    local bg = core.get_layer(output_name, "fluid_bg")
    bg:set("point_count", 1)
      :set("blend_power", 0.5 + (bass * 0.5)) -- Optional: Chain another parameter
end

return M