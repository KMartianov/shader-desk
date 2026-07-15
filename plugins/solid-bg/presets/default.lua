-- Default preset for Solid Bg
return {
    gradient_type = 3, -- 0: Solid, 1: Linear, 2: Radial, 3: 4-Corner Mesh
    color_space = 1, -- 0: sRGB (Standard), 1: Oklab (Perceptual smooth blending)
    color_1 = {30.89, 0.28, 0.20}, -- Primary Color (Top-Left)
    color_2 = {30.15, 0.40, 0.85}, -- Secondary Color (Bottom-Right)
    color_3 = {30.95, 0.75, 0.20}, -- Tertiary Color (Top-Right / Mesh only)
    color_4 = {30.60, 0.20, 0.80}, -- Quaternary Color (Bottom-Left / Mesh only)
    angle = 45.0, -- Angle in degrees (Linear only)
    radial_center = {20.5, 0.5}, -- Center position (Radial only)
    radial_radius = 1.2, -- Gradient spread radius (Radial only)
    dither_strength = 1.0, -- TPDF Dithering intensity (1.0 = smooth 8-bit)
}
