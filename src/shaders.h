// shaders.h  -  all GLSL (330 core) embedded as raw strings.
// NOTE: MSVC refuses a single string literal over ~16 KB, so every chunk below
// is kept well under that and the pieces are concatenated at run time.
#pragma once

static const char* GLSL_VERSION = "#version 330 core\n";

// ===========================================================================
// COMMON: noise + procedural sky (used by sky, scene and water shaders)
// ===========================================================================
static const char* GLSL_COMMON = R"GLSL(
uniform vec3  uSunDir;    // unit vector TOWARD the sun
uniform vec3  uMoonDir;   // unit vector TOWARD the moon
uniform float uTime;

float hash12(vec2 p){ vec3 p3 = fract(vec3(p.xyx)*0.1031); p3 += dot(p3, p3.yzx+33.33); return fract((p3.x+p3.y)*p3.z); }
float hash13(vec3 p3){ p3 = fract(p3*0.1031); p3 += dot(p3, p3.zyx+31.32); return fract((p3.x+p3.y)*p3.z); }

float vnoise(vec2 p){
    vec2 i = floor(p), f = fract(p);
    vec2 u = f*f*(3.0-2.0*f);
    return mix(mix(hash12(i), hash12(i+vec2(1,0)), u.x),
               mix(hash12(i+vec2(0,1)), hash12(i+vec2(1,1)), u.x), u.y);
}
float vnoise3(vec3 p){
    vec3 i = floor(p), f = fract(p);
    f = f*f*(3.0-2.0*f);
    float a = mix(hash13(i),                 hash13(i+vec3(1,0,0)), f.x);
    float b = mix(hash13(i+vec3(0,1,0)),     hash13(i+vec3(1,1,0)), f.x);
    float c = mix(hash13(i+vec3(0,0,1)),     hash13(i+vec3(1,0,1)), f.x);
    float d = mix(hash13(i+vec3(0,1,1)),     hash13(i+vec3(1,1,1)), f.x);
    return mix(mix(a,b,f.y), mix(c,d,f.y), f.z);
}
float fbm(vec2 p){
    float a = 0.5, s = 0.0;
    for(int i=0;i<5;i++){ s += a*vnoise(p); p = p*2.03 + vec2(17.1,9.2); a *= 0.5; }
    return s;
}

// ---- sky colours depend on the sun's elevation (night / sunset / day) ----
void skyPalette(out vec3 zenith, out vec3 horizon){
    float e = uSunDir.y;
    vec3 zDay = vec3(0.10,0.28,0.62), hDay = vec3(0.62,0.76,0.95);
    vec3 zSet = vec3(0.05,0.09,0.26), hSet = vec3(0.95,0.34,0.07);
    vec3 zNit = vec3(0.002,0.004,0.014), hNit = vec3(0.012,0.018,0.040);
    float toSet = smoothstep(-0.22, 0.0, e);
    float toDay = smoothstep(0.08, 0.45, e);
    zenith  = mix(mix(zNit, zSet, toSet), zDay, toDay);
    horizon = mix(mix(hNit, hSet, toSet), hDay, toDay);
}

// gradient + sun glow (no clouds / stars): used for ambient light, reflections, fog
vec3 skyBase(vec3 d){
    vec3 zen, hor;
    skyPalette(zen, hor);
    float e = uSunDir.y;
    float y = clamp(d.y, 0.0, 1.0);
    vec3 col = mix(hor, zen, pow(y, 0.62));
    float sd = max(dot(d, uSunDir), 0.0);
    float vis = smoothstep(-0.12, 0.05, e);
    vec3 sunCol = mix(vec3(1.0,0.38,0.08), vec3(1.0,0.92,0.75), smoothstep(0.0,0.5,e));
    float glow = pow(sd, 5.0)*0.30 + pow(sd, 40.0)*0.9;
    col += sunCol * glow * vis * mix(1.0, 0.35, smoothstep(0.1, 0.6, e));
    return col;
}

// full sky: base + stars + moon + sun disc + two cloud layers
vec3 skyFull(vec3 d){
    vec3 col = skyBase(d);
    vec3 base = col;
    float e = uSunDir.y;
    float night = 1.0 - smoothstep(-0.20, 0.0, e);
    float lightAmt = smoothstep(-0.14, 0.10, e);
    vec3 sunCol = mix(vec3(1.0,0.40,0.10), vec3(1.0,0.93,0.80), smoothstep(0.0,0.45,e));

    // stars
    if(night > 0.01 && d.y > 0.0){
        vec3 sp = d*260.0;
        vec3 id = floor(sp);
        float h = hash13(id);
        float st = step(0.9970, h);
        float dd = length(fract(sp)-0.5);
        st *= smoothstep(0.42, 0.0, dd);
        float mag = (h-0.997)/0.003;
        float tw = 0.75 + 0.25*sin(uTime*2.5 + h*60.0);
        col += vec3(0.85,0.9,1.0)*st*(0.4+1.6*mag)*tw*night*smoothstep(0.0,0.15,d.y);
    }

    // moon
    float md = dot(d, uMoonDir);
    float moonOn = smoothstep(-0.05, 0.10, uMoonDir.y);
    float mdisc = smoothstep(cos(0.034), cos(0.030), md);
    vec3 mcol = vec3(0.92,0.93,1.0)*(0.65+0.35*vnoise3(d*90.0))*2.2;
    col += vec3(0.45,0.55,0.9)*pow(max(md,0.0), 220.0)*0.35*moonOn*night;
    col = mix(col, mcol, mdisc*moonOn*mix(0.25,1.0,night));

    // clouds
    float cc = 0.0;
    if(d.y > 0.0){
        vec2 uv = d.xz/(d.y+0.18);
        vec2 p = (uv*0.9 + vec2(uTime*0.012, uTime*0.006))*1.1;
        float dens  = fbm(p);
        vec2 sdir = normalize(uSunDir.xz + vec2(1e-4));
        float dens2 = fbm(p + sdir*0.18);
        float cov1 = smoothstep(0.50, 0.78, dens);
        float shade = clamp(0.50 + (dens-dens2)*6.0, 0.0, 1.0);

        mat2 rot = mat2(0.82,-0.57,0.57,0.82);
        vec2 pc = rot*uv*vec2(0.35,1.9) + vec2(uTime*0.006, 0.0);
        float cir = fbm(pc*1.6);
        float cov2 = smoothstep(0.50, 0.80, cir)*0.65;

        float sunFac = pow(max(dot(normalize(vec3(d.x,0.0,d.z)), normalize(vec3(uSunDir.x,0.0,uSunDir.z)+vec3(1e-4))), 0.0), 2.0);
        vec3 dayLit = vec3(1.0,0.99,0.97)*1.25;
        vec3 setLit = mix(vec3(0.70,0.30,0.38), sunCol*1.7, sunFac);
        vec3 litCol = mix(setLit, dayLit, smoothstep(0.1, 0.45, e));
        vec3 shdCol = mix(vec3(0.10,0.08,0.15), vec3(0.50,0.57,0.70), smoothstep(0.05, 0.4, e));
        float dim = mix(0.04, 1.0, lightAmt);
        vec3 cloudCol = mix(shdCol, litCol, shade)*dim + vec3(0.02,0.025,0.04)*night;
        vec3 cirCol   = mix(shdCol*1.2, litCol, 0.8)*dim + vec3(0.02,0.025,0.04)*night;
        float fade = smoothstep(0.0, 0.2, d.y);
        float a1 = cov1*fade, a2 = cov2*fade;
        col = mix(col, cloudCol, a1*0.92);
        col = mix(col, cirCol, a2);
        cc = max(a1, a2);
    }

    // sun disc (added last so thin cloud can dim it)
    float sd = dot(d, uSunDir);
    float sdisc = smoothstep(cos(0.019), cos(0.012), sd);
    col += sunCol*sdisc*30.0*smoothstep(-0.03,-0.005,e)*(1.0-cc*0.85);
    return col;
}
)GLSL";


// ===========================================================================
// BEAM: the rotating lighthouse beam (shared by scene, water and the volumetric pass)
// Angle convention: dir = (cos a, 0, sin a); increasing a is CLOCKWISE seen from above.
// ===========================================================================
static const char* GLSL_BEAM = R"GLSL(
uniform float uBeamAngle;
uniform vec3  uLampPos;
uniform float uBeamOn;           // 0 = lamp off, 1 = full
const float BEAM_HALF = 0.115;   // half width in radians (~6.6 degrees, soft edge)

float beamAz(vec3 p){
    vec2 h = p.xz - uLampPos.xz;
    float rho = length(h);
    float c = dot(h/max(rho, 1e-3), vec2(cos(uBeamAngle), sin(uBeamAngle)));
    return smoothstep(cos(BEAM_HALF), cos(BEAM_HALF*0.30), c);
}
float beamEl(vec3 p){
    vec3 d = p - uLampPos;
    float e = atan(d.y, max(length(d.xz), 1e-3));
    return smoothstep(-1.15, -0.95, e)*(1.0 - smoothstep(-0.02, 0.06, e));   // wide downward fan: reaches the water, boat and island
}
float beamElVol(vec3 p){                                                   // narrower fan for the visible light shaft
    vec3 d = p - uLampPos;
    float e = atan(d.y, max(length(d.xz), 1e-3));
    return smoothstep(-0.62, -0.42, e)*(1.0 - smoothstep(-0.02, 0.06, e));
}
float beamAtt(float dist){ return (1.0/(1.0 + dist*dist/(35.0*35.0)))*(1.0 - smoothstep(110.0, 210.0, dist)); }
vec3 beamRadiance(vec3 p){
    float dist = length(p - uLampPos);
    return vec3(1.0, 0.88, 0.62)*28.0*beamAz(p)*beamEl(p)*beamAtt(dist)*uBeamOn;
}
)GLSL";

// ===========================================================================
// Island height (GLSL twin of islandHeight() in geometry.h)
// ===========================================================================
static const char* GLSL_ISLAND = R"GLSL(
float islandHeight(vec2 p){
    float r = length(p);
    float a = atan(p.y, p.x);
    float wob = 1.0 + 0.10*sin(3.0*a+0.7) + 0.06*sin(5.0*a+2.1) + 0.04*sin(9.0*a+4.0);
    float s = (r/wob)/18.0;
    return 9.5*exp(-s*s) - 3.5;
}
)GLSL";

// ===========================================================================
// Sky pass (full-screen triangle)
// ===========================================================================
static const char* SKY_VS = R"GLSL(
out vec2 vNdc;
void main(){
    vec2 p = vec2(float((gl_VertexID<<1)&2), float(gl_VertexID&2));
    vNdc = p*2.0-1.0;
    gl_Position = vec4(vNdc, 1.0, 1.0);
}
)GLSL";

static const char* SKY_FS = R"GLSL(
in vec2 vNdc;
out vec4 FragColor;
uniform mat4 uInvViewProj;
uniform vec3 uCamPos;
void main(){
    vec4 p = uInvViewProj*vec4(vNdc, 1.0, 1.0);
    vec3 dir = normalize(p.xyz/p.w - uCamPos);
    FragColor = vec4(skyFull(dir), 1.0);
}
)GLSL";

// ===========================================================================
// Shadow depth pass
// ===========================================================================
static const char* SHADOW_VS = R"GLSL(
layout(location=0) in vec3 aPos;
uniform mat4 uModel;
uniform mat4 uLightVP;
void main(){ gl_Position = uLightVP*uModel*vec4(aPos,1.0); }
)GLSL";
static const char* SHADOW_FS = R"GLSL(
void main(){}
)GLSL";

// ===========================================================================
// Scene pass: PBR + shadows + procedural materials
// ===========================================================================
static const char* SCENE_VS = R"GLSL(
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNrm;
layout(location=2) in vec2 aUV;
layout(location=3) in vec3 aCol;
uniform mat4 uModel;
uniform mat4 uViewProj;
out vec3 vWorld;
out vec3 vNormal;
out vec2 vUV;
out vec3 vCol;
void main(){
    vec4 wp = uModel*vec4(aPos,1.0);
    vWorld = wp.xyz;
    vNormal = mat3(uModel)*aNrm;
    vUV = aUV;
    vCol = aCol;
    gl_Position = uViewProj*wp;
    gl_ClipDistance[0] = wp.y + 0.15;   // used only in the water-reflection pass
}
)GLSL";

static const char* SCENE_FS = R"GLSL(
in vec3 vWorld;
in vec3 vNormal;
in vec2 vUV;
in vec3 vCol;
out vec4 FragColor;

uniform vec3  uCamPos;
uniform int   uMaterial;
uniform float uEmissive;
uniform float uLamp;          // 0 = day, 1 = night (lamps on)
uniform vec3  uLightDir;      // toward dominant light (sun or moon)
uniform vec3  uLightColor;    // its radiance
uniform mat4  uLightVP;
uniform sampler2D uShadowMap;
uniform float uFogDensity;

const float PI = 3.14159265;

float shadowFactor(vec3 wp, vec3 N, vec3 L){
    vec4 sp = uLightVP*vec4(wp + N*0.08, 1.0);
    vec3 pc = sp.xyz/sp.w*0.5 + 0.5;
    if(pc.z > 1.0 || pc.x < 0.0 || pc.x > 1.0 || pc.y < 0.0 || pc.y > 1.0) return 1.0;
    float bias = 0.0005 + 0.0015*(1.0 - max(dot(N,L),0.0));
    float texel = 1.0/float(textureSize(uShadowMap,0).x);
    float sum = 0.0;
    for(int x=-2;x<=2;x++){
        for(int y=-2;y<=2;y++){
            float d = texture(uShadowMap, pc.xy + vec2(x,y)*texel*1.3).r;
            sum += (pc.z - bias > d) ? 0.0 : 1.0;
        }
    }
    return sum/25.0;
}

vec3 pbr(vec3 albedo, float rough, float metal, vec3 N, vec3 V, vec3 L, vec3 radiance){
    vec3 H = normalize(V+L);
    float NdL = max(dot(N,L),0.0);
    float NdV = max(dot(N,V),1e-4);
    float NdH = max(dot(N,H),0.0);
    float VdH = max(dot(V,H),0.0);
    vec3 F0 = mix(vec3(0.04), albedo, metal);
    float a = rough*rough, a2 = a*a;
    float dd = NdH*NdH*(a2-1.0)+1.0;
    float D = a2/(PI*dd*dd);
    float k = (rough+1.0)*(rough+1.0)/8.0;
    float G = (NdL/(NdL*(1.0-k)+k))*(NdV/(NdV*(1.0-k)+k));
    vec3 F = F0 + (1.0-F0)*pow(1.0-VdH, 5.0);
    vec3 spec = D*G*F/(4.0*NdL*NdV + 1e-4);
    vec3 kd = (1.0-F)*(1.0-metal);
    return (kd*albedo/PI + spec)*radiance*NdL;
}

vec3 ambientDiffuse(vec3 N){
    vec3 up  = skyBase(vec3(0.0,1.0,0.0));
    vec3 hor = skyBase(normalize(vec3(N.x, 0.02, N.z)));
    vec3 ground = hor*0.30*vec3(0.85,0.75,0.65);
    float t = N.y*0.5+0.5;
    return mix(ground, mix(hor, up, 0.5), t);
}

vec3 bumpNormal(vec3 N, vec3 p, float strength){
    float e = 0.02;
    float h0 = vnoise3(p);
    vec3 g = vec3(vnoise3(p+vec3(e,0,0))-h0, vnoise3(p+vec3(0,e,0))-h0, vnoise3(p+vec3(0,0,e))-h0)/e;
    g -= N*dot(g,N);
    return normalize(N - g*strength);
}

float wrapAng(float a){ return atan(sin(a), cos(a)); }

void main(){
    vec3 N = normalize(vNormal);
    vec3 V = normalize(uCamPos - vWorld);
    vec3 albedo = vCol;
    float rough = 0.6, metal = 0.0, ao = 1.0;
    vec3 emis = vec3(0.0);

    if(uMaterial == 0){                       // ---- striped plaster tower ----
        float y = vUV.y;
        float ang = wrapAng(vUV.x*6.2831853);
        float rad = mix(2.85, 1.85, clamp((y-1.3)/19.3, 0.0, 1.0));
        if(y < 1.32){                         // stone plinth
            albedo = vec3(0.32,0.30,0.28)*(0.7+0.6*fbm(vWorld.xz*1.5+vWorld.y));
            rough = 0.9;
            N = bumpNormal(N, vWorld*4.0, 0.5);
        } else {
            float bandF = (y-1.3)/3.2;
            float band = floor(bandF);
            float isRed = 1.0 - step(0.5, fract(band*0.5));
            vec3 red = vec3(0.60,0.05,0.04), white = vec3(0.85,0.83,0.78);
            albedo = mix(white, red, isRed);
            float grime  = fbm(vec2(vUV.x*24.0, y*0.9));
            float streak = fbm(vec2(vUV.x*90.0, y*0.12));
            albedo *= 0.78 + 0.32*grime;
            albedo *= mix(0.55, 1.0, smoothstep(1.3, 7.0, y));
            albedo *= mix(0.70, 1.0, streak);
            rough = 0.72;
            N = bumpNormal(N, vWorld*5.0, 0.18);
            // door
            if(y > 1.4 && y < 3.9){
                float lat = abs(ang)*rad;
                if(lat < 0.78){
                    albedo = vec3(0.30,0.28,0.26); rough = 0.85;
                    float top = 3.45 - lat*lat*0.9;
                    if(lat < 0.60 && y < top){ albedo = vec3(0.10,0.05,0.025); rough = 0.6; }
                }
            }
            // windows: 3 rows x 3 sides
            for(int k=0;k<3;k++){
                float lat = abs(wrapAng(ang - float(k)*2.0943951))*rad;
                for(int w=0; w<3; w++){
                    float wy = 8.2 + float(w)*4.8;
                    float dy = abs(y - wy);
                    if(lat < 0.42 && dy < 0.72){
                        albedo = vec3(0.82,0.80,0.75); rough = 0.5;
                        if(lat < 0.30 && dy < 0.60){
                            albedo = vec3(0.01,0.015,0.02); rough = 0.05;
                            emis = vec3(1.0,0.72,0.33)*uLamp*0.9;
                        }
                    }
                }
            }
        }
    }
    else if(uMaterial == 1){ rough = 0.38; metal = 0.85; }          // dark metal
    else if(uMaterial == 2){                                         // glass
        albedo = vec3(0.015,0.02,0.03); rough = 0.04;
        float lens = 1.0;
        if(uEmissive > 5.0){                                         // lantern: brightest where the beam points
            vec2 hh = normalize(vWorld.xz - uLampPos.xz + vec2(1e-4));
            lens = 0.25 + 1.6*pow(max(dot(hh, vec2(cos(uBeamAngle), sin(uBeamAngle))), 0.0), 8.0);
        }
        emis = vec3(1.0,0.72,0.32)*uEmissive*uLamp*lens*(0.8+0.4*vnoise(vec2(vUV.x*40.0, vUV.y*3.0)));
    }
    else if(uMaterial == 8){ albedo = vec3(0.02); rough = 0.3; emis = vCol*uEmissive*uLamp; }   // nav lights
    else if(uMaterial == 3){ rough = 0.35; metal = 0.55; }          // roof
    else if(uMaterial == 4){                                         // ---- terrain ----
        float h = vWorld.y;
        float slope = 1.0 - N.y;
        float n1 = fbm(vWorld.xz*0.35), n2 = fbm(vWorld.xz*1.7), n3 = vnoise(vWorld.xz*9.0);
        vec3 sand = vec3(0.55,0.47,0.33), dry = vec3(0.40,0.38,0.17), grass = vec3(0.16,0.25,0.07);
        vec3 rockc = vec3(0.27,0.25,0.23), wetc = vec3(0.20,0.16,0.10);
        vec3 c = mix(sand, dry, smoothstep(0.8, 1.8, h + (n1-0.5)*2.5));
        c = mix(c, grass, smoothstep(2.2, 4.2, h + (n1-0.5)*3.0)*(1.0-smoothstep(0.25,0.45,slope)));
        c = mix(c, rockc*(0.7+0.6*n2), smoothstep(0.22, 0.42, slope + (n2-0.5)*0.15));
        c *= 0.82 + 0.36*n3;
        float wet = 1.0 - smoothstep(0.0, 0.5, h + (n2-0.5)*0.3);
        c = mix(c, wetc, wet*0.75);
        albedo = c;
        rough = mix(0.92, 0.35, wet);
        N = bumpNormal(N, vec3(vWorld.x, vWorld.y*2.0, vWorld.z)*3.0, 0.35);
        float r = length(vWorld.xz);
        ao = mix(0.45, 1.0, smoothstep(3.3, 7.5, r));
    }
    else if(uMaterial == 5){                                         // ---- rock ----
        float n = fbm(vWorld.xz*1.3 + vWorld.y*0.7);
        albedo = mix(vec3(0.20,0.19,0.18), vec3(0.42,0.38,0.32), n)*(0.75+0.5*vnoise3(vWorld*5.0));
        float wetB = 1.0 - smoothstep(0.0, 0.9, vWorld.y);
        albedo = mix(albedo, vec3(0.07,0.10,0.04), wetB*0.55);
        rough = 0.88 - wetB*0.3;
        N = bumpNormal(N, vWorld*2.5, 0.7);
        ao = mix(0.6, 1.0, smoothstep(-0.2, 1.0, vWorld.y));
    }
    else if(uMaterial == 6){                                         // ---- painted planks ----
        float g = fract(vUV.y*5.5);
        float groove = smoothstep(0.0,0.06,g)*smoothstep(0.0,0.06,1.0-g);
        float grain = vnoise(vec2(vUV.x*2.0, vUV.y*50.0));
        albedo = vCol*(0.62+0.38*groove)*(0.88+0.24*grain);
        rough = 0.42;
    }
    else { rough = 0.55; }                                           // generic painted

    vec3 L = normalize(uLightDir);
    float sh = shadowFactor(vWorld, N, L);
    vec3 col = pbr(albedo, rough, metal, N, V, L, uLightColor)*sh;

    float NdV = max(dot(N,V), 0.0);
    vec3 F0 = mix(vec3(0.04), albedo, metal);
    vec3 kd = (1.0-F0)*(1.0-metal);
    col += kd*albedo*ambientDiffuse(N)*ao*0.9;
    vec3 R = reflect(-V, N);
    vec3 env = skyBase(vec3(R.x, max(R.y,0.0), R.z));
    vec3 Fr = F0 + (max(vec3(1.0-rough), F0) - F0)*pow(1.0-NdV, 5.0);
    col += env*Fr*ao*(1.0 - rough*0.6);
    // the rotating beam as a spotlight on this surface
    vec3 bR = beamRadiance(vWorld);
    if(dot(bR, bR) > 1e-6){
        vec3 Lb = normalize(uLampPos - vWorld);
        col += pbr(albedo, rough, metal, N, V, Lb, bR);
    }
    col += emis;

    float dist = length(uCamPos - vWorld);
    vec3 dir = -V;
    vec3 fogC = skyBase(vec3(dir.x, 0.02, dir.z));
    col = mix(col, fogC, 1.0 - exp(-dist*uFogDensity));
    FragColor = vec4(col, 1.0);
}
)GLSL";

// ===========================================================================
// Water
// ===========================================================================
static const char* WATER_VS = R"GLSL(
layout(location=0) in vec3 aPos;
uniform mat4 uViewProj;
out vec3 vWorld;
void main(){
    vWorld = aPos;
    gl_Position = uViewProj*vec4(aPos,1.0);
}
)GLSL";

static const char* WATER_FS = R"GLSL(
in vec3 vWorld;
out vec4 FragColor;
uniform vec3  uCamPos;
uniform vec2  uResolution;
uniform sampler2D uReflection;
uniform vec3  uLightDir;
uniform vec3  uLightColor;
uniform float uWaveAmp;
uniform float uFogDensity;
uniform vec2  uBoatPos;
uniform vec2  uBoatFwd;
uniform float uBoatSpeed;

// sum of directional sine waves; returns the height gradient (d h / d xz)
vec2 waveGrad(vec2 p, float t, float amp, float dist){
    const float wl[6] = float[6](38.0, 21.0, 11.0, 6.0, 3.1, 1.7);
    const float am[6] = float[6](0.30, 0.25, 0.18, 0.12, 0.07, 0.04);
    const float an[6] = float[6](0.30, 1.00, -0.60, 2.00, -1.40, 0.70);
    vec2 g = vec2(0.0);
    for(int i=0;i<6;i++){
        float k = 6.2831853/wl[i];
        vec2 dir = vec2(cos(an[i]), sin(an[i]));
        float w = sqrt(9.81*k);
        float ph = k*dot(dir,p) - w*t + float(i)*1.7 + 3.0*vnoise(p*0.04 + float(i)*3.7);
        float fade = smoothstep(dist*0.03, dist*0.09, wl[i]);   // kill sub-pixel waves far away
        g += dir*(am[i]*amp*k*cos(ph))*fade;
    }
    return g;
}

void main(){
    vec3 wp = vWorld;
    vec3 toCam = uCamPos - wp;
    float dist = length(toCam);
    vec3 V = toCam/dist;

    vec2 g = waveGrad(wp.xz, uTime, uWaveAmp, dist);
    float micro = 1.0/(1.0 + dist*0.03);
    vec2 q1 = wp.xz*0.9 + vec2(uTime*0.35, uTime*0.22);
    vec2 q2 = wp.xz*2.3 - vec2(uTime*0.28, -uTime*0.40);
    float e = 0.05;
    vec2 g1 = vec2(vnoise(q1+vec2(e,0))-vnoise(q1-vec2(e,0)), vnoise(q1+vec2(0,e))-vnoise(q1-vec2(0,e)))/(2.0*e);
    vec2 g2 = vec2(vnoise(q2+vec2(e,0))-vnoise(q2-vec2(e,0)), vnoise(q2+vec2(0,e))-vnoise(q2-vec2(0,e)))/(2.0*e);
    g += (g1*0.035 + g2*0.018)*uWaveAmp*micro;

    float depth = -islandHeight(wp.xz);                 // water depth above the sea bed
    g *= mix(0.3, 1.0, smoothstep(0.0, 4.0, depth));    // calmer in shallows
    vec3 N = normalize(vec3(-g.x, 1.0, -g.y));

    float NdV = clamp(dot(N,V), 0.0, 1.0);
    float F = 0.02 + 0.98*pow(1.0-NdV, 5.0);

    // planar reflection (scene rendered upside-down), distorted by the ripples
    vec2 uv = gl_FragCoord.xy/uResolution;
    uv += N.xz*0.05/(1.0 + dist*0.02);
    vec3 refl = texture(uReflection, clamp(uv, 0.001, 0.999)).rgb;

    // body colour: what you see "through" the surface
    vec3 skyUp = skyBase(vec3(0.0,1.0,0.0));
    vec3 deepC = vec3(0.012,0.065,0.095), shalC = vec3(0.06,0.28,0.26);
    vec3 body = mix(shalC, deepC, smoothstep(0.0, 7.0, depth));
    float dayLight = clamp(uLightDir.y, 0.0, 1.0);
    vec3 bodyLit = body*(skyUp*0.9 + uLightColor*dayLight*0.10 + 0.02);

    // sun / moon glitter
    vec3 L = normalize(uLightDir);
    vec3 H = normalize(L+V);
    float nh = max(dot(N,H), 0.0);
    float spec = pow(nh, 700.0)*(708.0)/(8.0*3.14159);
    spec = min(spec, 40.0);
    vec3 specC = uLightColor*spec*0.15*F*step(0.0, L.y);

    // shoreline foam
    float shore = 1.0 - smoothstep(0.0, 0.9, depth);
    float nf = fbm(wp.xz*1.8 + vec2(uTime*0.2, -uTime*0.15));
    float pulse = 0.5 + 0.5*sin(depth*3.0 - uTime*1.2 + nf*4.0);
    float foam = smoothstep(0.45, 0.8, nf*0.7 + shore*0.6 + pulse*0.2*shore)
               * smoothstep(-0.2, 0.05, depth) * (1.0 - smoothstep(0.7, 1.4, depth));
    // boat wake: turbulent centre + V-shaped edge waves, fading with distance behind the boat
    vec2 rel = wp.xz - uBoatPos;
    float lx = dot(rel, uBoatFwd);
    float lz = dot(rel, vec2(-uBoatFwd.y, uBoatFwd.x));
    float behind = -lx - 2.5;
    float wake = 0.0;
    if(behind > 0.0 && behind < 34.0){
        float spread = 0.9 + behind*0.16;
        float c = abs(lz);
        float core = smoothstep(spread, 0.0, c);
        float arms = smoothstep(0.35, 0.0, abs(c - spread*0.9))*0.8;
        float fadeLen = 1.0 - behind/34.0;
        float nz = fbm(wp.xz*2.2 + vec2(uTime*0.6, -uTime*0.4));
        wake = (core*0.7 + arms)*fadeLen*smoothstep(0.35, 0.75, nz + 0.25)*clamp(uBoatSpeed*1.3, 0.0, 1.0);
    }
    foam = max(foam, wake);
    vec3 bR = beamRadiance(wp);
    vec3 foamC = (skyUp*0.7 + uLightColor*dayLight*0.12 + 0.04)*0.9 + bR*0.12;

    // lantern beam sweeping over the water: soft glow + a glittering streak
    vec3 Lb = normalize(uLampPos - wp);
    vec3 Hb = normalize(Lb + V);
    float spb = pow(max(dot(N, Hb), 0.0), 220.0)*228.0/(8.0*3.14159);
    vec3 beamWater = bR*0.012 + bR*min(spb, 25.0)*0.006*F;

    vec3 col = bodyLit*(1.0-F) + refl*F + specC + beamWater;
    col = mix(col, foamC, foam*0.85);
    float alpha = max(smoothstep(0.0, 1.4, depth), foam);

    vec3 dir = -V;
    vec3 fogC = skyBase(vec3(dir.x, 0.02, dir.z));
    col = mix(col, fogC, 1.0 - exp(-dist*uFogDensity));
    FragColor = vec4(col, alpha);
}
)GLSL";

// ===========================================================================
// Post: bloom + tonemap
// ===========================================================================
static const char* POST_VS = R"GLSL(
out vec2 vUv;
void main(){
    vec2 p = vec2(float((gl_VertexID<<1)&2), float(gl_VertexID&2));
    vUv = p;
    gl_Position = vec4(p*2.0-1.0, 0.0, 1.0);
}
)GLSL";

static const char* EXTRACT_FS = R"GLSL(
in vec2 vUv;
out vec4 FragColor;
uniform sampler2D uTex;
uniform float uExposure;
void main(){
    vec3 c = texture(uTex, vUv).rgb*uExposure;
    float l = dot(c, vec3(0.2126,0.7152,0.0722));
    FragColor = vec4(c*smoothstep(1.0, 3.0, l), 1.0);
}
)GLSL";

static const char* COPY_FS = R"GLSL(
in vec2 vUv;
out vec4 FragColor;
uniform sampler2D uTex;
void main(){ FragColor = vec4(texture(uTex, vUv).rgb, 1.0); }
)GLSL";

static const char* BLUR_FS = R"GLSL(
in vec2 vUv;
out vec4 FragColor;
uniform sampler2D uTex;
uniform vec2 uDir;     // one texel step along the blur direction
void main(){
    float w[5] = float[5](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);
    vec3 c = texture(uTex, vUv).rgb*w[0];
    for(int i=1;i<5;i++){
        c += texture(uTex, vUv + uDir*float(i)).rgb*w[i];
        c += texture(uTex, vUv - uDir*float(i)).rgb*w[i];
    }
    FragColor = vec4(c, 1.0);
}
)GLSL";

static const char* COMPOSITE_FS = R"GLSL(
in vec2 vUv;
out vec4 FragColor;
uniform sampler2D uScene;
uniform sampler2D uBloom0;
uniform sampler2D uBloom1;
uniform sampler2D uBloom2;
uniform sampler2D uBloom3;
uniform float uExposure;
uniform float uBloomStrength;

float hash12(vec2 p){ vec3 p3 = fract(vec3(p.xyx)*0.1031); p3 += dot(p3, p3.yzx+33.33); return fract((p3.x+p3.y)*p3.z); }

vec3 aces(vec3 x){ return clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14), 0.0, 1.0); }

void main(){
    vec3 hdr = texture(uScene, vUv).rgb*uExposure;
    vec3 bloom = texture(uBloom0, vUv).rgb*0.50
               + texture(uBloom1, vUv).rgb*0.40
               + texture(uBloom2, vUv).rgb*0.30
               + texture(uBloom3, vUv).rgb*0.25;
    vec3 col = hdr + bloom*uBloomStrength;
    col = aces(col);
    col = pow(col, vec3(1.0/2.2));
    float lum = dot(col, vec3(0.2126,0.7152,0.0722));
    col = mix(vec3(lum), col, 1.18);
    float v = smoothstep(1.25, 0.35, length((vUv-0.5)*vec2(1.25,1.0)));
    col *= mix(0.72, 1.0, v);
    col += (hash12(gl_FragCoord.xy)-0.5)/255.0;     // dither: no banding in the sky gradient
    FragColor = vec4(col, 1.0);
}
)GLSL";

// ===========================================================================
// Volumetric beam: ray-march the light shaft through hazy air (additive pass).
// The wedge-shaped beam is intersected analytically with each view ray, so the
// marching only happens where there is actually light.
// ===========================================================================
static const char* BEAM_FS = R"GLSL(
in vec2 vUv;
out vec4 FragColor;
uniform sampler2D uDepth;
uniform mat4 uInvViewProj;
uniform vec3 uCamPos;
uniform float uTime;
uniform float uHaze;

float hash12(vec2 p){ vec3 p3 = fract(vec3(p.xyx)*0.1031); p3 += dot(p3, p3.yzx+33.33); return fract((p3.x+p3.y)*p3.z); }
float hg(float c, float g){ float g2 = g*g; return (1.0-g2)/(12.566371*pow(1.0 + g2 - 2.0*g*c, 1.5)); }

void main(){
    FragColor = vec4(0.0, 0.0, 0.0, 1.0);
    if(uBeamOn < 0.01) return;

    float depth = texture(uDepth, vUv).r;
    vec4 pw = uInvViewProj*vec4(vUv*2.0-1.0, depth*2.0-1.0, 1.0);
    vec3 P = pw.xyz/pw.w;
    vec3 ray = P - uCamPos;
    float tScene = length(ray);
    vec3 dir = ray/tScene;
    if(depth >= 0.99999) tScene = 2000.0;

    // horizontal wedge limits: points p with cross(e1,p) >= 0 and cross(p,e2) >= 0
    vec2 o = uCamPos.xz - uLampPos.xz;
    vec2 v = dir.xz;
    float w = BEAM_HALF*1.7;
    vec2 e1 = vec2(cos(uBeamAngle - w), sin(uBeamAngle - w));
    vec2 e2 = vec2(cos(uBeamAngle + w), sin(uBeamAngle + w));
    float a1 = e1.x*o.y - e1.y*o.x, b1 = e1.x*v.y - e1.y*v.x;
    float a2 = o.x*e2.y - o.y*e2.x, b2 = v.x*e2.y - v.y*e2.x;
    float t0 = 0.0, t1 = tScene;
    if(abs(b1) < 1e-7){ if(a1 < 0.0) return; } else { float tt = -a1/b1; if(b1 > 0.0) t0 = max(t0, tt); else t1 = min(t1, tt); }
    if(abs(b2) < 1e-7){ if(a2 < 0.0) return; } else { float tt = -a2/b2; if(b2 > 0.0) t0 = max(t0, tt); else t1 = min(t1, tt); }

    // keep to a sphere of radius 320 m around the lamp
    vec3 oc = uCamPos - uLampPos;
    float bq = dot(oc, dir), cq = dot(oc, oc) - 320.0*320.0;
    float disc = bq*bq - cq;
    if(disc < 0.0) return;
    float sq = sqrt(disc);
    t0 = max(t0, -bq - sq);
    t1 = min(t1, -bq + sq);
    if(t1 <= t0) return;

    const int N = 36;
    float dt = (t1 - t0)/float(N);
    float jit = hash12(gl_FragCoord.xy + fract(uTime)*17.0);
    float acc = 0.0;
    for(int i = 0; i < N; i++){
        float t = t0 + (float(i) + jit)*dt;
        vec3 p = uCamPos + dir*t;
        float az = beamAz(p);
        if(az <= 0.0) continue;
        float el = beamElVol(p);
        float dist = length(p - uLampPos);
        vec3 ld = (p - uLampPos)/max(dist, 1e-3);
        float cosT = dot(ld, -dir);
        float phase = 0.15 + hg(cosT, 0.72);
        float dens = uHaze*(0.45 + 0.9*exp(-max(p.y, 0.0)*0.04));
        acc += az*el*beamAtt(dist)*dens*phase*dt;
    }
    FragColor = vec4(vec3(1.0, 0.88, 0.62)*28.0*uBeamOn*acc, 1.0);
}
)GLSL";