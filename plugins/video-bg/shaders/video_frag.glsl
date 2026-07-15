#version 300 es
precision highp float;

in vec2 v_uv;
in vec2 v_sdf_pos;

out vec4 FragColor;

// ==============================================================================
// УНИФОРМЫ
// ==============================================================================
uniform sampler2D base_image;
uniform vec2 pixel_size; 
uniform float screen_aspect; 
uniform float tex_aspect;    

uniform int fill_mode;
uniform float border_radius;

uniform float brightness;
uniform float contrast;
uniform float saturation;
uniform vec3 tint_color;
uniform float tint_intensity;
uniform float blur_radius;

const vec3 LUMA_COEFFS = vec3(0.2126, 0.7152, 0.0722);

float sdRoundRect(vec2 p, vec2 b, float r) {
    vec2 d = abs(p) - b + vec2(r);
    return min(max(d.x, d.y), 0.0) + length(max(d, 0.0)) - r;
}

void main() {
    float alpha_mask = 1.0;

    // --------------------------------------------------------------------------
    // ОПТИМИЗАЦИЯ: Считаем математику SDF ТОЛЬКО если задано скругление.
    // Если видео на весь экран (border_radius == 0), мы пропускаем тяжелые расчеты.
    // --------------------------------------------------------------------------
    if (border_radius > 0.001) {
        vec2 sdf_p = v_sdf_pos;
        sdf_p.x *= screen_aspect;
        
        vec2 box_size = vec2(screen_aspect, 1.0);
        float dist = sdRoundRect(sdf_p, box_size, border_radius * 2.0);
        
        alpha_mask = 1.0 - smoothstep(0.0, pixel_size.y * 3.0, dist);

        // Ранний выход за пределами скругленной карточки
        if (alpha_mask <= 0.001) {
            FragColor = vec4(0.0);
            return;
        }
    }

    // 2. ВПИСЫВАНИЕ ВИДЕО (COVER / CONTAIN)
    vec2 uv = v_uv;
    uv -= 0.5;
    if (fill_mode == 0) { // COVER
        if (screen_aspect > tex_aspect) uv.y *= (1.0 / screen_aspect) * tex_aspect;
        else                            uv.x *= screen_aspect / tex_aspect;
    } else { // CONTAIN
        if (screen_aspect > tex_aspect) uv.x *= screen_aspect / tex_aspect;
        else                            uv.y *= tex_aspect / screen_aspect;
    }
    uv += 0.5;

    // Ранний выход для пустых зон Contain
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        FragColor = vec4(0.0);
        return;
    }

    // 3. ОДНОКРАТНАЯ ВЫБОРКА (если блюр выключен)
    vec4 tex_color;
    if (blur_radius > 0.001) {
        tex_color = texture(base_image, uv);
        const vec2 offsets[8] = vec2[8](
            vec2(-1.0, -1.0), vec2(-1.0, 1.0), vec2(1.0, -1.0), vec2(1.0, 1.0),
            vec2(1.0, 0.0), vec2(0.0, -1.0), vec2(-1.0, 0.0), vec2(0.0, 1.0)
        );
        vec2 step_offset = (1.0 / vec2(textureSize(base_image, 0))) * blur_radius * 5.0;
        float weight = 1.0;
        for(int i = 0; i < 8; i++) {
            tex_color += texture(base_image, uv + offsets[i] * step_offset);
            weight += 1.0;
        }
        tex_color /= weight;
    } else {
        tex_color = texture(base_image, uv); // Самый дешевый путь конвейера
    }

    if (tex_color.a < 0.005) {
        FragColor = vec4(0.0);
        return;
    }

    // 4. ЦВЕТОКОРРЕКЦИЯ
    vec3 final_color = tex_color.rgb;
    if (contrast != 1.0 || brightness != 1.0 || saturation != 1.0 || tint_intensity > 0.0) {
        final_color = ((final_color - 0.5) * contrast + 0.5) * brightness;
        float luma = dot(final_color, LUMA_COEFFS);
        final_color = mix(vec3(luma), final_color, saturation);
        final_color = mix(final_color, tint_color, tint_intensity);
    }

    FragColor = vec4(final_color, tex_color.a * alpha_mask);
}