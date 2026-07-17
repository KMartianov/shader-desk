-- Default preset for Dither Effect
return {
    shader_theme = "dither", -- Which shader fragment to load
    dither_spread = 0.5, -- Noise intensity (0.0 = banding, 1.0 = heavy noise)
    downsample_scale = 3.0, -- Pixel size (1.0 = native, 4.0 = retro)
    bayer_size = 1, -- Bayer Matrix resolution: 0 = 2x2, 1 = 4x4, 2 = 8x8
    colors_count = 4, -- Number of active colors in the palette (2 to 16)
}
