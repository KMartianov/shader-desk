#version 300 es
precision highp float;

in vec2 v_uv;
out vec4 FragColor;

uniform sampler2D u_prev_layer;
uniform vec2 resolution;
uniform float time;

uniform float intensity; 
uniform float speed;     
uniform float scale;     
uniform int variant;     // 0: Rain, 1: Frosted Glass

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f); 

    return mix(
        mix(hash(i + vec2(0.0, 0.0)), hash(i + vec2(1.0, 0.0)), u.x),
        mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), u.x),
        u.y
    );
}

vec2 noise_gradient(vec2 p) {
    float eps = 0.01; 
    float nX = noise(p + vec2(eps, 0.0)) - noise(p - vec2(eps, 0.0));
    float nY = noise(p + vec2(0.0, eps)) - noise(p - vec2(0.0, eps));
    return vec2(nX, nY) / (2.0 * eps);
}

void main() {
    vec2 aspect = vec2(resolution.x / resolution.y, 1.0);
    vec2 uv = v_uv;
    
    vec2 noise_uv = uv * aspect * scale;
    
    if (variant == 0) {
        noise_uv.y -= time * speed;
        noise_uv.x += sin(time * speed * 0.5) * 0.5;
    } else {
        noise_uv += noise(noise_uv + time * speed) * 0.5;
    }

    vec2 refraction = noise_gradient(noise_uv);
    vec2 distorted_uv = clamp(uv + refraction * (intensity * 0.05), 0.0, 1.0);

    vec4 texel = texture(u_prev_layer, distorted_uv);

    // Specular Highlights (only apply if the pixel actually has geometry behind it)
    float highlight = max(0.0, dot(normalize(refraction), vec2(-0.5, 0.5)));
    highlight = pow(highlight, 4.0) * intensity * 0.5;
    
    vec3 final_color = texel.rgb + vec3(highlight * texel.a);

    // Perserve original alpha channel
    FragColor = vec4(final_color, texel.a);
}