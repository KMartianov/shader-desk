
// https://www.shadertoy.com/view/tfBXzD
// CC0: Fork B of - A space flower
// Credit for the original to: mrange (https://www.shadertoy.com/user/mrange/)

// I first only created this fork because the question mark (?)
// in the originalname prevents an import into ISF.
// The license is identical with the original, the CCO-license, 
// I changed two or three values in this B-Fork version from the original-code. 
// & I marked the changes with a short note in the code after them 
// with "bH". your bennoH. 2024-03-10 with respect!!

//  @byt3m3chanic doing pretty cool with variable cell size truchets.
//  Didn't really understand what he did so I tried something inspired by it
//  I both failed and succeeded. It didn't turn out as intended but kind of neat anyway.


// See @byt3m3chanic's tweet: https://twitter.com/byt3m3chanic/status/1676035945320046592?s=20

// Music: Fearbace & Nostre - Still Isolated  
// This sound dont works good for my Fork "B ...", to fast, sry.
//
// Maybe that's a good option: https://www.youtube.com/watch?v=bUnRHfROyHY
// or: https://www.youtube.com/watch?v=tUbWjhkOBM0
// & a difrend Tip: https://www.youtube.com/watch?v=cH2guTTyX8s 
// or mabye like that for some: https://www.youtube.com/watch?v=gtWLkBYAj0Y
// but to somthing like that funky sound works: https://www.youtube.com/watch?v=GA4-PX2VQe8&pp=ygUsRXhlY3V0aXZlIEZ1bmsgLSBNeSB2ZXJ5IHNwZWNpYWwgTWlzcyAoMjAyNSk%3D
// you can load it in a second BrowserTab - it's so easy !!
// if I work with that as a ISF I do speedcontroll and a lot of craycy stuff to synchronize it 
// " make effects by codeoptions to the sound as a liveperformances for normal, thats my part of that art


#define TIME        iTime
#define RESOLUTION  iResolution
#define PI          3.141592654
#define TAU         (2.0*PI)
#define ROT(a)      mat2(cos(a), sin(a), -sin(a), cos(a))
#define REV(x)      exp2((x)*zoom)
#define FWD(x)      (log2(x)/zoom)

const float zoom = log2(3.8);  // 3.8 was original 1.3  bH

#define REP 10.0       // 6.0 original it was 20.0 bH
#define KALEIDOSCOPE

// License: Unknown, author: Unknown, found: don't remember
float hash(vec2 co) {
  return fract(sin(dot(co.xy ,vec2(12.9898,58.233))) * 13758.5453);
}

vec2 toPolar(vec2 p) {
  return vec2(length(p), atan(p.y, p.x));
}

vec2 toRect(vec2 p) {
  return vec2(p.x*cos(p.y), p.x*sin(p.y));    // cos anstelle sin wäre interessant bH
}

// License: MIT, author: Inigo Quilez, found: https://www.iquilezles.org/www/articles/smin/smin.htm
float pmin(float a, float b, float k) {
  float h = clamp(0.5+0.5*(b-a)/k, -0.00, 1.0);  // -0.30 was original 0.0  bH
  return mix(b, a, h) - k*h*(1.0-h);
}

float pabs(float a, float k) {
  return -pmin(a, -a, k);
}

// License: MIT OR CC-BY-NC-4.0, author: mercury, found: https://mercury.sexy/hg_sdf/
float modMirror1(inout float p, float size) {
  float halfsize = size*1.2;
  float c = floor((p + halfsize)/size);
  p = mod(p + halfsize,size) - halfsize;
  p *= mod(c, 2.0)*4.4 - 1.0;    // 4.4 was original 2.00
  return c;
}

float smoothKaleidoscope(inout vec2 p, float sm, float rep) {
  vec2 hp = p;

  vec2 hpp = toPolar(hp);
  float rn = modMirror1(hpp.y, TAU/rep);

  float sa = PI/rep - pabs(PI/rep - abs(hpp.y), sm);
  hpp.y = sign(hpp.y)*(sa);

  hp = toRect(hpp);

  p = hp;

  return rn;
}

// License: MIT, author: Inigo Quilez, found: https://iquilezles.org/www/articles/distfunctions2d/distfunctions2d.htm
float segment(vec2 p, vec2 a, vec2 b) {
  vec2 pa = p-a, ba = b-a;
  float h = clamp(dot(pa,ba)/dot(ba,ba), 0.20, 1.0); // 0.20 was original 0.0  bH
  return length(pa - ba*h);
}

vec3 effect(vec2 p, vec2 pp) {
  vec2 op = p;
  float tm = TIME;
  float mtm = fract(tm);
  float ftm = floor(tm);

  mat2 rot = ROT(1.067*TIME);  // 1.067 was original 0.075 bH

  const float rep = REP;
  const float sm  = 0.1*36.0/REP;
  p *= transpose(rot);
#if defined(KALEIDOSCOPE)
  float nn = smoothKaleidoscope(p, sm, rep);
#endif  
  p *= rot;
  p *= ROT(-0.5*length(p));
  p += 0.5*cos(vec2(1.0, sqrt(0.5))*TIME*0.18);    // cos was original sin & 0.18 was original 0.08

  
  float zz = REV(mtm);
  vec2 p2 = p;
  p2 /= zz;
  vec2 s2 = sign(p2);
  p2 = abs(p2);
  vec2 fp2 = FWD((p2));

  vec2 n = floor(fp2);
  float h = hash(s2.x+s2.y+n-ftm);
  
  vec2 x0 = REV(n);
  vec2 x1 = REV(n+1.0);

  vec2 m = (x0+x1)*0.5;
  vec2 w = x1-x0;

  vec2 modi = h > 0.5 ? vec2(1.0, 1.0) : vec2(1.0, -1.0);

  vec2 p3 = p2;
  p3 -= m;
  p3 = abs(p3);
  p3 -= 0.5*w;
  float d3 = length(p3);

  vec2 p4 = p2;
  p4 -= m;
  float d4 = segment(p4, -0.30*w*modi, 0.5*w*modi);  // -0.30 was original -0.5 bH
  d4 = min(d4, d4);
  d4 *= zz;
  
  float d6 = min(abs(p.x), abs(p.y));
  vec3 col = vec3(-0.04);    // -0.04 was original 0.00  bH
  
  float fo =1.0 - exp(-10.0*(d6-0.02));
  float ll = length(pp);
  vec3 gcol4 = 0.0025*(1.0+cos(vec3(0.0, 1.0, 2.0)+TIME+TAU*h+ll));
  vec3 gcol6 = 0.005*(1.0+cos(vec3(0.0, 1.0, 2.0)+TIME+ll));
  
  col += fo*gcol4/max(d4, 0.001);
  col = clamp(col, 0.0, 1.0);
  col += gcol6/max(d6, 0.0001);
  col = clamp(col, 0.0, 1.0);
  col -= 0.01*vec3(0.0,1.0,2.0).zyx*(ll);     // 0.01 was origianl 0.05  bH    
  col = sqrt(col);
  return col;
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
  vec2 q = fragCoord/RESOLUTION.xy;
  vec2 p = -1. + 2.0 * q;
  vec2 pp = p;
  p.x *= RESOLUTION.x/RESOLUTION.y;
  vec3 col = effect(p, pp);
  fragColor = vec4(col, 0.20);     //    0.20 was original 1.0  bH
}
