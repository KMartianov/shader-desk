#version 300 es
precision highp float;
precision highp int; // <--- ИСПРАВЛЕНИЕ ОШИБКИ ЛИНКОВКИ

in vec2 v_uv;
out vec4 FragColor;

uniform sampler2D base_image;
uniform int fill_mode;
uniform float brightness;
uniform float contrast;
uniform float saturation;
uniform vec3 tint_color;
uniform float tint_intensity;
uniform float blur_radius;

const vec3 LUMA_COEFFS = vec3(0.2126, 0.7152, 0.0722);

void main() {
    vec2 uv = v_uv;
    
    // В режиме Tile мы просто зацикливаем UV
    if (fill_mode == 3) {
        uv = fract(uv);
    } else {
        // Защита: отсекаем пиксели, вылезшие за UV [0..1] (актуально для режима Cover)
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
            discard;
        }
    }

    // Zero-Cost аппаратное размытие
    vec4 tex_color;
    if (blur_radius > 0.001) {
        float mip_level = log2(blur_radius * 0.5 + 1.0) * 1.5;
        tex_color = textureLod(base_image, uv, mip_level);
    } else {
        tex_color = texture(base_image, uv);
    }

    // CULLING 2: Отбрасываем внутреннюю альфу PNG картинки (срезает Overdraw)
    if (tex_color.a < 0.005) {
        discard;
    }

    // Оптимизированная цветокоррекция
    if (contrast != 1.0 || brightness != 1.0 || saturation != 1.0 || tint_intensity > 0.0) {
        vec3 final_color = tex_color.rgb;
        final_color = ((final_color - 0.5) * contrast + 0.5) * brightness;
        float luma = dot(final_color, LUMA_COEFFS);
        final_color = mix(vec3(luma), final_color, saturation);
        final_color = mix(final_color, tint_color, tint_intensity);
        tex_color.rgb = final_color;
    }

    FragColor = tex_color;
}