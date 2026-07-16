-- ==============================================================================
-- SCENE: Live Stream Parallax
-- Прямая трансляция с YouTube в качестве обоев с реакцией на мышь.
-- ==============================================================================

local M = {
    meta = {
        name = "Live Stream Parallax",
        author = "Shader Desk",
        version = "1.0",
    },

    layers = {
        {
            effect = "Video Bg",
            tag = "stream",
            settings = {
                -- Lofi Girl Live Stream (или любой другой URL)
                video_path = "https://www.youtube.com/live/X4VbdwhkE10?si=pFYP6HW5qZypYEhs",
                
                fill_mode = 0,
                is_muted = true,
                
                -- ВАЖНО: Делаем масштаб чуть больше 1.0, чтобы при сдвиге
                -- видео (параллакс) по краям не появлялись черные рамки
                scale = 1.00,
                debug_mpv = true 
            }
        },
       
    },
    
    state = {
        smoothed_mx = 0.0,
        smoothed_my = 0.0
    }
}

M.on_frame = function(self, dt, output_name)
    -- Читаем мышь (Pointer Provider)
    local target_x = core.get_float("mouse.accum_x", 0.0) * 0.02
    local target_y = core.get_float("mouse.accum_y", 0.0) * 0.02

    -- Ограничиваем сдвиг, чтобы не вылезти за пределы scale = 1.05
    target_x = math.max(-0.02, math.min(0.02, target_x))
    target_y = math.max(-0.02, math.min(0.02, target_y))

    -- Инерция (Плавное скольжение видео за мышью)
    self.state.smoothed_mx = self.state.smoothed_mx + (target_x - self.state.smoothed_mx) * 5.0 * dt
    self.state.smoothed_my = self.state.smoothed_my + (target_y - self.state.smoothed_my) * 5.0 * dt

    -- Zero-Allocation сдвиг UV координат видео на лету
    core.get_layer(output_name, "stream")
        :set_vec2("offset", self.state.smoothed_mx, self.state.smoothed_my)
end

return M