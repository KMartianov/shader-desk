#version 300 es
// High precision is mandatory for spherical trigonometry calculations
precision highp float;

// --- Vertex input attributes ---
layout (location = 0) in vec3 aPos;
layout (location = 1) in float aPhase; 
layout (location = 2) in vec3 aNormal; 

// --- Standard matrices ---
uniform mat4 model;      
uniform mat4 view;       
uniform mat4 projection; 
uniform float time;

// --- Effect parameters from the configuration file (STRICTLY ORIGINAL!) ---
// @param oscill_amp | float | 0.0 | Base oscillation amplitude.
uniform float oscill_amp;
// @param oscill_freq | float | 1.0 | Base oscillation frequency.
uniform float oscill_freq;
// @param wave_amp | float | 0.0 | Wave effect amplitude.
uniform float wave_amp;
// @param wave_freq | float | 10.0 | Frequency (density) of surface waves.
uniform float wave_freq;
// @param twist_amp | float | 0.0 | Twisting effect strength.
uniform float twist_amp;
// @param pulse_amp | float | 0.0 | Base pulsation amplitude.
uniform float pulse_amp;
// @param noise_amp | float | 0.0 | Noise deformation amplitude.
uniform float noise_amp;
// @param sphere_scale | float | 1.0 | Overall sphere scale.
uniform float sphere_scale;

// --- Uniform variables for audio reaction ---
uniform float audio_bass;
uniform float audio_mid;
uniform float audio_treble;
uniform float audio_bands[64];

out vec3 FragPos; 

// ============================================================================
// 1. SPHERICAL PHYSICS LIBRARY
// ============================================================================

// Convert Cartesian coordinates to spherical
vec2 get_spherical(vec3 p) {
    float r = length(p);
    // Clamp is mandatory so acos doesn't yield NaN due to float inaccuracies
    float theta = acos(clamp(p.y / r, -1.0, 1.0)); // Latitude: [0, PI]
    float phi = atan(p.z, p.x);                    // Longitude: [-PI, PI]
    return vec2(theta, phi);
}

// Dynamic spherical harmonic l=3, m=2 (Tetrahedral symmetry)
float sh_dynamic_3_2(vec2 sph, float phase_shift) {
    float sin_t = sin(sph.x);
    // 15.0 - mathematical coefficient for this shape
    return 15.0 * sin_t * sin_t * cos(sph.x) * cos(2.0 * sph.y + phase_shift);
}

// Dynamic spherical harmonic l=4, m=3 (Octahedral symmetry)
float sh_dynamic_4_3(vec2 sph, float phase_shift) {
    float sin_t = sin(sph.x);
    return 105.0 * sin_t * sin_t * sin_t * cos(sph.x) * cos(3.0 * sph.y + phase_shift);
}

// ============================================================================
// 2. TRAVELING WAVES (Geodesic ripples)
// ============================================================================
// Calculates a decaying sine wave traveling along the shortest arc from the center point
float geodesic_ripple(vec3 p, vec3 center, float freq, float speed) {
    float dist = acos(clamp(dot(p, center), -1.0, 1.0));
    // Exp(-dist * 1.5) - natural wave decay factor
    return exp(-dist * 1.5) * sin(dist * freq - time * speed);
}

// ============================================================================
// 3. 3D SIMPLEX NOISE
// ============================================================================
vec4 permute(vec4 x){return mod(((x*34.0)+1.0)*x, 289.0);}
vec4 taylorInvSqrt(vec4 r){return 1.79284291400159 - 0.85373472095314 * r;}
float snoise(vec3 v){ 
  const vec2  C = vec2(1.0/6.0, 1.0/3.0);
  const vec4  D = vec4(0.0, 0.5, 1.0, 2.0);
  vec3 i  = floor(v + dot(v, C.yyy));
  vec3 x0 = v - i + dot(i, C.xxx);
  vec3 g = step(x0.yzx, x0.xyz);
  vec3 l = 1.0 - g;
  vec3 i1 = min( g.xyz, l.zxy );
  vec3 i2 = max( g.xyz, l.zxy );
  vec3 x1 = x0 - i1 + 1.0 * C.xxx;
  vec3 x2 = x0 - i2 + 2.0 * C.xxx;
  vec3 x3 = x0 - 1.0 + 3.0 * C.xxx;
  i = mod(i, 289.0); 
  vec4 p = permute( permute( permute( 
             i.z + vec4(0.0, i1.z, i2.z, 1.0 ))
           + i.y + vec4(0.0, i1.y, i2.y, 1.0 )) 
           + i.x + vec4(0.0, i1.x, i2.x, 1.0 ));
  float n_ = 1.0/7.0; 
  vec3  ns = n_ * D.wyz - D.xzx;
  vec4 j = p - 49.0 * floor(p * ns.z *ns.z);
  vec4 x_ = floor(j * ns.z);
  vec4 y_ = floor(j - 7.0 * x_ );
  vec4 x = x_ *ns.x + ns.yyyy;
  vec4 y = y_ *ns.x + ns.yyyy;
  vec4 h = 1.0 - abs(x) - abs(y);
  vec4 b0 = vec4( x.xy, y.xy );
  vec4 b1 = vec4( x.zw, y.zw );
  vec4 s0 = floor(b0)*2.0 + 1.0;
  vec4 s1 = floor(b1)*2.0 + 1.0;
  vec4 sh = -step(h, vec4(0.0));
  vec4 a0 = b0.xzyw + s0.xzyw*sh.xxyy ;
  vec4 a1 = b1.xzyw + s1.xzyw*sh.zzww ;
  vec3 p0 = vec3(a0.xy,h.x);
  vec3 p1 = vec3(a0.zw,h.y);
  vec3 p2 = vec3(a1.xy,h.z);
  vec3 p3 = vec3(a1.zw,h.w);
  vec4 norm = taylorInvSqrt(vec4(dot(p0,p0), dot(p1,p1), dot(p2, p2), dot(p3,p3)));
  p0 *= norm.x; p1 *= norm.y; p2 *= norm.z; p3 *= norm.w;
  vec4 m = max(0.6 - vec4(dot(x0,x0), dot(x1,x1), dot(x2,x2), dot(x3,x3)), 0.0);
  m = m * m;
  return 42.0 * dot( m*m, vec4( dot(p0,x0), dot(p1,x1), dot(p2,x2), dot(p3,x3) ) );
}

void main()
{
    vec3 scaledPos = aPos * sphere_scale;
    vec3 normPos = normalize(aPos); 
    
    // Inject the twist_amp parameter into spherical coordinate calculations.
    // This creates pole twisting around the Y axis depending on the Lua setting.
    vec3 twistedPos = normPos;
    float twist_angle = normPos.y * twist_amp * sin(time * 0.5);
    float cos_t = cos(twist_angle);
    float sin_t = sin(twist_angle);
    twistedPos.x = normPos.x * cos_t - normPos.z * sin_t;
    twistedPos.z = normPos.x * sin_t + normPos.z * cos_t;
    
    vec2 sph = get_spherical(twistedPos);

    float total_disp = 0.0;

    // ========================================================================
    // 1. RESONANCE (Spherical harmonics)
    // ========================================================================
    // Use oscill_freq to control the rotation speed of the shapes, 
    // And oscill_amp acts as a multiplier/amplifier from the user.
    float rot_phase = time * max(oscill_freq, 0.5); 
    
    float bass_resonance = audio_bass * sh_dynamic_3_2(sph, rot_phase * 2.0);
    float mid_resonance  = audio_mid  * sh_dynamic_4_3(sph, -rot_phase * 3.0);
    
    // Pole flattening for heavy bass (spherical harmonic l=2, m=0)
    float pole_squash = audio_bass * 0.5 * (3.0 * twistedPos.y * twistedPos.y - 1.0);
    
    // Base coefficient 0.15 + user oscill_amp
    float res_multiplier = 0.15 + oscill_amp;
    total_disp += (bass_resonance + mid_resonance - pole_squash) * res_multiplier;

    // ========================================================================
    // 2. TRAVELING WAVES (Impact ripples)
    // ========================================================================
    // Use wave_freq for wave density, wave_amp as an amplifier
    vec3 north_pole = vec3(0.0, 1.0, 0.0);
    vec3 south_pole = vec3(0.0, -1.0, 0.0);
    
    // Base ripple frequency = 15.0. If wave_freq = 0, ripple is standard.
    float actual_wave_freq = 15.0 + wave_freq;
    
    float ripple_N = geodesic_ripple(twistedPos, north_pole, actual_wave_freq, 8.0);
    float ripple_S = geodesic_ripple(twistedPos, south_pole, actual_wave_freq, 8.0);
    
    // Base coefficient 0.2 + user wave_amp
    total_disp += audio_bass * (ripple_N + ripple_S) * (0.2 + wave_amp);

    // ========================================================================
    // 3. ORGANIC SPECTRAL MAPPING (64 Frequencies)
    // ========================================================================
    // 3D noise is used as a coordinate to smoothly and chaotically 
    // "stretch" 64 equalizer bands across the sphere's surface.
    float spatial_noise = snoise(twistedPos * 1.5 + time * 0.2); // [-1..1]
    float normalized_noise = (spatial_noise + 1.0) * 0.5;        // [0..1]
    
    float exact_idx = normalized_noise * 63.0;
    int idx1 = int(exact_idx);
    int idx2 = min(idx1 + 1, 63);
    float blend = fract(exact_idx); 
    
    // Exact audio band energy for this point on the sphere
    float band_energy = mix(audio_bands[idx1], audio_bands[idx2], blend);
    
    // Create micro-relief. noise_amp from Lua amplifies the spectrum's influence.
    float micro_surface = sin(sph.x * 20.0) * cos(sph.y * 20.0);
    total_disp += band_energy * micro_surface * (0.4 + noise_amp);

    // ========================================================================
    // 4. TREBLE AND PULSE
    // ========================================================================
    // Fine high-frequency noise
    total_disp += audio_treble * snoise(twistedPos * 8.0 - time * 5.0) * 0.15;
    
    // Custom pulse_amp adds a simple "breathing" effect to the entire sphere
    total_disp += sin(time * max(oscill_freq, 1.0)) * pulse_amp;

    // ========================================================================
    // APPLICATION AND OUTPUT
    // ========================================================================
    // Geometry protection (so vertices don't cross the sphere center)
    total_disp = clamp(total_disp, -0.7, 1.5);

    // Push the vertex along the normal
    vec3 displacedPos = scaledPos + normPos * total_disp;
    
    FragPos = vec3(model * vec4(displacedPos, 1.0));
    gl_Position = projection * view * vec4(FragPos, 1.0);
}