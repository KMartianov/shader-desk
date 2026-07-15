-- Default preset for Image Bg
return {
    base_image_path = "default.jpg", -- Main background image
    fill_mode = 0, -- 0: Cover, 1: Contain, 2: Stretch, 3: Tile
    scale = 1.0, -- Image scale (Zoom)
    offset = {20.0, 0.0}, -- UV Offset (Pan X/Y)
    rotation = 0.0, -- Rotation in degrees
    brightness = 1.0, -- Brightness multiplier
    contrast = 1.0, -- Contrast adjustment
    saturation = 1.0, -- Color saturation (0.0 = Grayscale)
    tint_color = {30.0, 0.0, 0.0}, -- Tint color (RGB)
    tint_intensity = 0.0, -- Tint blending intensity (0.0 - 1.0)
    blur_radius = 0.0, -- Gaussian blur intensity
}
