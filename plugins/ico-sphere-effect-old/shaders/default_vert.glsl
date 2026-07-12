// Specify the GLSL ES version (OpenGL for Embedded Systems)
#version 300 es

// --- Vertex input attributes ---
layout (location = 0) in vec3 aPos;
layout (location = 1) in float aPhase; // Left for C++ compatibility
layout (location = 2) in vec3 aNormal; // Left for C++ compatibility

// --- Standard matrices ---
uniform mat4 model;      
uniform mat4 view;       
uniform mat4 projection; 

// Time variable for animation
uniform float time;

// --- Effect parameters from the configuration file (Do not delete comments!) ---
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
uniform float audio_bands[64]; // Array of all 64 frequencies

out vec3 FragPos; 


// --- 3D Simplex Noise (Great organic noise) ---
vec4 permute(vec4 x){return mod(((x*34.0)+1.0)*x, 289.0);}
vec4 taylorInvSqrt(vec4 r){return 1.79284291400159 - 0.85373472095314 * r;}
float snoise(vec3 v){ 
  const vec2  C = vec2(1.0/6.0, 1.0/3.0) ;
  const vec4  D = vec4(0.0, 0.5, 1.0, 2.0);
  vec3 i  = floor(v + dot(v, C.yyy) );
  vec3 x0 = v - i + dot(i, C.xxx) ;
  vec3 g = step(x0.yzx, x0.xyz);
  vec3 l = 1.0 - g;
  vec3 i1 = min( g.xyz, l.zxy );
  vec3 i2 = max( g.xyz, l.zxy );
  vec3 x1 = x0 - i1 + 1.0 * C.xxx;
  vec3 x2 = x0 - i2 + 2.0 * C.xxx;
  vec3 x3 = x0 - 1.0 + 3.0 * C.xxx;
  i = mod(i, 289.0 ); 
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
    // Scale the sphere, but use a perfect sphere (normalized vector) for calculations
    vec3 scaledPos = aPos * sphere_scale;
    vec3 normPos = normalize(aPos); 
    
    // --- 1. ORGANIC EQUALIZER ---
    // Y Modulus: 0.0 at the equator, 1.0 at the poles
    float mappedY = abs(normPos.y); 
    
    // Warp the frequency reading itself! This breaks strict rings into beautiful "islands"
    float bandWarp = snoise(normPos * 3.0 - time * 0.2) * 0.15;
    
    // Calculate index from 0 to 63
    int bandIndex = int(clamp((mappedY + bandWarp) * 63.0, 0.0, 63.0));
    
    // Extract the frequency. pow() makes the "spikes" visually sharper and more rhythmic
    float local_freq = audio_bands[bandIndex];
    float spike_disp = pow(local_freq, 1.5) * 0.8;

    // --- 2. BASE ORGANICS (NOISE) ---
    // Deep, smooth bass warps the sphere's shape itself (like a water drop)
    float bass_warp = snoise(normPos * 1.2 + time * 0.8) * (audio_bass * 0.4);
    
    // Light breathing in silence to make the sphere feel alive
    float idle_morph = snoise(normPos * 2.5 - time * 0.1) * 0.08;

    // --- 3. USER SETTINGS FROM LUA ---
    // Use normPos.y so the wave pattern doesn't break when sphere_scale changes
    float config_wave = sin(normPos.y * wave_freq + time * 2.0) * wave_amp;
    float config_noise = snoise(normPos * 4.0 + time) * noise_amp;
    float config_twist = sin(normPos.y * 8.0 + time * 1.5) * twist_amp;
    float config_pulse = sin(time * oscill_freq) * pulse_amp;

    // --- SUM ALL DEFORMATIONS ---
    float total_disp = idle_morph + bass_warp + spike_disp + 
                       config_wave + config_noise + config_twist + config_pulse;
    
    // Displace the vertex from the center (normPos equals the normal for a sphere at 0,0,0)
    vec3 displacedPos = scaledPos + normPos * total_disp;
    
    FragPos = vec3(model * vec4(displacedPos, 1.0));
    gl_Position = projection * view * vec4(FragPos, 1.0);
}