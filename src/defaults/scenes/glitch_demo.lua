return {
    layers = {
        {
            effect = "Hilbert Cube", -- Рисуем 3D куб
            tag = "base_3d"
        },
        {
            effect = "Postprocess Effect", -- Накидываем сверху глитч
            tag = "glitch_filter",
            postprocess = true,         -- КРИТИЧЕСКИ ВАЖНО! Включит Ping-Pong FBO
            settings = {
                variant = 2,
                intensity = 0.3,
                speed = 1.5,
                scale = 20.0
            }
        }
    }
}