// main.cpp  -  Lighthouse scene, Stage B3: animation (boat + rotating beam)
//
// Render passes per frame:
//   1. shadow map        (depth from the sun / moon)
//   2. reflection        (scene mirrored in the water plane)
//   3. main HDR pass     (sky -> PBR scene -> water), 4x MSAA
//   4. light shaft       (ray-marched volumetric lighthouse beam, additive)
//   5. bloom             (bright pixels blurred at 4 scales)
//   6. tonemap + gamma   (ACES filmic) to the window
//
// Controls:  mouse drag / arrows = orbit   scroll or - = = zoom   C = follow the boat
//            Space = pause   R = restart boat   F = fast-forward boat (x4)
//            Z / X = beam slower / faster   L = force lamp on (even in daylight)
//            1 dawn  2 noon  3 sunset  4 night  5 dusk   , . = change time   T = auto time
//            [ ] = wave size   W = wireframe   P = screenshot   Esc = quit
// Command line (optional):  --shot out.png --time 18.6 --anim 60 --beam 0.0 --lamp
//                           --yaw 1.57 --pitch -0.09 --dist 82 --w 1600 --h 900

#if __has_include(<glad/gl.h>)
  #include <glad/gl.h>                       // glad 2.x
  #define LOAD_GL() gladLoadGL((GLADloadfunc)glfwGetProcAddress)
#else
  #include <glad/glad.h>                     // glad 0.1.x (vcpkg default)
  #define LOAD_GL() gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)
#endif
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "geometry.h"
#include "shaders.h"

// ---------------------------------------------------------------------------
// GL helpers
// ---------------------------------------------------------------------------
struct Mesh {
    GLuint vao = 0, vbo = 0, ebo = 0;
    GLsizei count = 0;
    void upload(const MeshData& d) {
        if (d.v.empty()) return;
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glGenBuffers(1, &ebo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, d.v.size() * sizeof(Vertex), d.v.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, d.i.size() * sizeof(uint32_t), d.i.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, pos));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, nrm));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, uv));
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, col));
        count = (GLsizei)d.i.size();
        glBindVertexArray(0);
    }
    void draw() const {
        if (!count) return;
        glBindVertexArray(vao);
        glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, 0);
    }
};

struct Program {
    GLuint id = 0;
    std::unordered_map<std::string, GLint> cache;
    GLint loc(const char* n) {
        auto it = cache.find(n);
        if (it != cache.end()) return it->second;
        GLint l = glGetUniformLocation(id, n);
        cache[n] = l;
        return l;
    }
    void use() const { glUseProgram(id); }
    void f1(const char* n, float v) { glUniform1f(loc(n), v); }
    void i1(const char* n, int v) { glUniform1i(loc(n), v); }
    void f2(const char* n, glm::vec2 v) { glUniform2f(loc(n), v.x, v.y); }
    void f3(const char* n, glm::vec3 v) { glUniform3f(loc(n), v.x, v.y, v.z); }
    void m4(const char* n, const glm::mat4& m) { glUniformMatrix4fv(loc(n), 1, GL_FALSE, glm::value_ptr(m)); }
};

static GLuint compileShader(GLenum type, const std::string& src, const char* name) {
    GLuint s = glCreateShader(type);
    const char* c = src.c_str();
    glShaderSource(s, 1, &c, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        std::fprintf(stderr, "Shader compile error in %s:\n%s\n", name, log);
        std::exit(1);
    }
    return s;
}

static Program makeProgram(const std::string& vs, const std::string& fs, const char* name) {
    GLuint v = compileShader(GL_VERTEX_SHADER, vs, name);
    GLuint f = compileShader(GL_FRAGMENT_SHADER, fs, name);
    Program p;
    p.id = glCreateProgram();
    glAttachShader(p.id, v);
    glAttachShader(p.id, f);
    glLinkProgram(p.id);
    GLint ok = 0;
    glGetProgramiv(p.id, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetProgramInfoLog(p.id, sizeof(log), nullptr, log);
        std::fprintf(stderr, "Program link error in %s:\n%s\n", name, log);
        std::exit(1);
    }
    glDeleteShader(v);
    glDeleteShader(f);
    return p;
}

// ---------------------------------------------------------------------------
// Render targets
// ---------------------------------------------------------------------------
struct BloomLevel {
    GLuint fbo[2] = {0, 0}, tex[2] = {0, 0};
    int w = 0, h = 0;
};

struct Targets {
    int w = 0, h = 0;
    GLuint msFbo = 0, msColor = 0, msDepth = 0;
    GLuint hdrFbo = 0, hdrTex = 0;
    GLuint depthFbo = 0, depthTex = 0;      // resolved (single-sample) depth for the beam pass
    GLuint reflFbo = 0, reflTex = 0, reflDepth = 0;
    BloomLevel bloom[4];
    GLuint shadowFbo = 0, shadowTex = 0;
    int shadowSize = 4096;
};

static GLuint makeTex(int w, int h, GLenum internalFmt, GLenum fmt, GLenum type) {
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, internalFmt, w, h, 0, fmt, type, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}

static void checkFbo(const char* name) {
    GLenum s = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (s != GL_FRAMEBUFFER_COMPLETE) {
        std::fprintf(stderr, "Framebuffer %s incomplete (0x%x)\n", name, s);
        std::exit(1);
    }
}

static void destroyScreenTargets(Targets& T) {
    if (T.msFbo) glDeleteFramebuffers(1, &T.msFbo);
    if (T.msColor) glDeleteRenderbuffers(1, &T.msColor);
    if (T.msDepth) glDeleteRenderbuffers(1, &T.msDepth);
    if (T.hdrFbo) glDeleteFramebuffers(1, &T.hdrFbo);
    if (T.hdrTex) glDeleteTextures(1, &T.hdrTex);
    if (T.depthFbo) glDeleteFramebuffers(1, &T.depthFbo);
    if (T.depthTex) glDeleteTextures(1, &T.depthTex);
    if (T.reflFbo) glDeleteFramebuffers(1, &T.reflFbo);
    if (T.reflTex) glDeleteTextures(1, &T.reflTex);
    if (T.reflDepth) glDeleteRenderbuffers(1, &T.reflDepth);
    for (auto& b : T.bloom) {
        for (int k = 0; k < 2; ++k) {
            if (b.fbo[k]) glDeleteFramebuffers(1, &b.fbo[k]);
            if (b.tex[k]) glDeleteTextures(1, &b.tex[k]);
        }
        b = BloomLevel();
    }
    T.msFbo = T.msColor = T.msDepth = T.hdrFbo = T.hdrTex = T.depthFbo = T.depthTex = T.reflFbo = T.reflTex = T.reflDepth = 0;
}

static void createScreenTargets(Targets& T, int w, int h) {
    destroyScreenTargets(T);
    T.w = w; T.h = h;
    GLint maxS = 4;
    glGetIntegerv(GL_MAX_SAMPLES, &maxS);
    int samples = std::min(4, (int)maxS);

    // multisampled HDR target
    glGenFramebuffers(1, &T.msFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, T.msFbo);
    glGenRenderbuffers(1, &T.msColor);
    glBindRenderbuffer(GL_RENDERBUFFER, T.msColor);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA16F, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, T.msColor);
    glGenRenderbuffers(1, &T.msDepth);
    glBindRenderbuffer(GL_RENDERBUFFER, T.msDepth);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH_COMPONENT32F, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, T.msDepth);
    checkFbo("msaa");

    // resolved HDR texture
    glGenFramebuffers(1, &T.hdrFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, T.hdrFbo);
    T.hdrTex = makeTex(w, h, GL_RGBA16F, GL_RGBA, GL_FLOAT);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, T.hdrTex, 0);
    checkFbo("hdr");

    // resolved depth (same format as the MSAA depth so the blit is legal)
    glGenFramebuffers(1, &T.depthFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, T.depthFbo);
    glGenTextures(1, &T.depthTex);
    glBindTexture(GL_TEXTURE_2D, T.depthTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, w, h, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, T.depthTex, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    checkFbo("depth");

    // planar reflection target
    glGenFramebuffers(1, &T.reflFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, T.reflFbo);
    T.reflTex = makeTex(w, h, GL_RGBA16F, GL_RGBA, GL_FLOAT);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, T.reflTex, 0);
    glGenRenderbuffers(1, &T.reflDepth);
    glBindRenderbuffer(GL_RENDERBUFFER, T.reflDepth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, T.reflDepth);
    checkFbo("reflection");

    // bloom chain: 1/2, 1/4, 1/8, 1/16 resolution
    for (int i = 0; i < 4; ++i) {
        BloomLevel& b = T.bloom[i];
        b.w = std::max(1, w >> (i + 1));
        b.h = std::max(1, h >> (i + 1));
        for (int k = 0; k < 2; ++k) {
            b.tex[k] = makeTex(b.w, b.h, GL_RGBA16F, GL_RGBA, GL_FLOAT);
            glGenFramebuffers(1, &b.fbo[k]);
            glBindFramebuffer(GL_FRAMEBUFFER, b.fbo[k]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, b.tex[k], 0);
            checkFbo("bloom");
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

static void createShadowTarget(Targets& T) {
    glGenFramebuffers(1, &T.shadowFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, T.shadowFbo);
    glGenTextures(1, &T.shadowTex);
    glBindTexture(GL_TEXTURE_2D, T.shadowTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, T.shadowSize, T.shadowSize, 0,
                 GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    float border[4] = {1, 1, 1, 1};
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, T.shadowTex, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    checkFbo("shadow");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// ---------------------------------------------------------------------------
// Application state
// ---------------------------------------------------------------------------
static float gYaw = 1.5708f, gPitch = -0.09f, gDist = 82.0f;
static float gTime = 18.6f;           // hour of day, 0..24 (18.6 = dusk, lantern already lit)
static bool  gAutoTime = false;
static float gWaveAmp = 0.15f;
static bool  gWire = false;
static bool  gShotRequested = false;
static float gAnimT = 0.0f;           // animation clock in seconds (drives the boat)
static bool  gPaused = false;
static bool  gFast = false;
static float gBeamAngle = 0.0f;       // radians; increasing = clockwise seen from above
static float gBeamPeriod = 8.0f;      // seconds per full rotation
static bool  gForceLamp = false;
static bool  gFollow = false;
static bool  gDrag = false;
static double gLastX = 0, gLastY = 0;

static void onKey(GLFWwindow* w, int key, int, int action, int) {
    if (action == GLFW_RELEASE) return;
    bool press = action == GLFW_PRESS;
    switch (key) {
        case GLFW_KEY_ESCAPE: glfwSetWindowShouldClose(w, 1); break;
        case GLFW_KEY_W: if (press) gWire = !gWire; break;
        case GLFW_KEY_T: if (press) gAutoTime = !gAutoTime; break;
        case GLFW_KEY_P: if (press) gShotRequested = true; break;
        case GLFW_KEY_SPACE: if (press) gPaused = !gPaused; break;
        case GLFW_KEY_R: if (press) gAnimT = 0.0f; break;
        case GLFW_KEY_F: if (press) gFast = !gFast; break;
        case GLFW_KEY_L: if (press) gForceLamp = !gForceLamp; break;
        case GLFW_KEY_C: if (press) { gFollow = !gFollow; gDist = gFollow ? 32.0f : 82.0f; } break;
        case GLFW_KEY_Z: gBeamPeriod = std::min(30.0f, gBeamPeriod * 1.05f); break;
        case GLFW_KEY_X: gBeamPeriod = std::max(2.0f, gBeamPeriod / 1.05f); break;
        case GLFW_KEY_5: gTime = 18.6f; break;
        case GLFW_KEY_1: gTime = 6.2f; break;
        case GLFW_KEY_2: gTime = 12.0f; break;
        case GLFW_KEY_3: gTime = 17.7f; break;
        case GLFW_KEY_4: gTime = 2.5f; break;
        case GLFW_KEY_COMMA: gTime = std::fmod(gTime - 0.1f + 24.0f, 24.0f); break;
        case GLFW_KEY_PERIOD: gTime = std::fmod(gTime + 0.1f, 24.0f); break;
        case GLFW_KEY_LEFT_BRACKET: gWaveAmp = std::max(0.05f, gWaveAmp - 0.05f); break;
        case GLFW_KEY_RIGHT_BRACKET: gWaveAmp = std::min(2.5f, gWaveAmp + 0.05f); break;
        case GLFW_KEY_MINUS: gDist = std::min(250.0f, gDist * 1.05f); break;
        case GLFW_KEY_EQUAL: gDist = std::max(12.0f, gDist / 1.05f); break;
        default: break;
    }
}
static void onMouseButton(GLFWwindow* w, int button, int action, int) {
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        gDrag = (action == GLFW_PRESS);
        glfwGetCursorPos(w, &gLastX, &gLastY);
    }
}
static void onCursor(GLFWwindow*, double x, double y) {
    if (gDrag) {
        gYaw -= (float)(x - gLastX) * 0.005f;
        gPitch += (float)(y - gLastY) * 0.004f;
        gPitch = std::clamp(gPitch, -0.2f, 1.35f);
    }
    gLastX = x; gLastY = y;
}
static void onScroll(GLFWwindow*, double, double dy) {
    gDist = std::clamp(gDist * std::pow(0.92f, (float)dy), 12.0f, 250.0f);
}

// ---------------------------------------------------------------------------
// Lighting from the time of day
// ---------------------------------------------------------------------------
struct Lighting {
    glm::vec3 sunDir, moonDir, lightDir, lightColor;
    float lamp, exposure;
};

static Lighting computeLighting(float tod) {
    Lighting L;
    float phi = (tod - 6.0f) / 12.0f * kPi;               // 0 = sunrise, pi/2 = noon, pi = sunset
    L.sunDir = glm::normalize(glm::vec3(std::cos(phi) * 0.95f, std::sin(phi), 0.33f));
    float mphi = phi - kPi;                                // moon is opposite the sun, on a lower arc
    L.moonDir = glm::normalize(glm::vec3(std::cos(mphi) * 0.95f, std::sin(mphi) * 0.35f, 0.33f));

    float sy = L.sunDir.y;
    float sunI = glm::smoothstep(-0.02f, 0.10f, sy);
    glm::vec3 sunCol = glm::mix(glm::vec3(1.0f, 0.38f, 0.12f), glm::vec3(1.0f, 0.95f, 0.86f),
                                glm::smoothstep(0.0f, 0.45f, sy));
    float sunPow = glm::mix(2.6f, 4.4f, glm::smoothstep(0.0f, 0.5f, sy));
    glm::vec3 sunRad = sunCol * sunPow * sunI;

    float moonI = glm::smoothstep(0.0f, 0.15f, L.moonDir.y) * (1.0f - sunI);
    glm::vec3 moonRad = glm::vec3(0.45f, 0.58f, 1.0f) * 0.55f * moonI;

    if (glm::length(sunRad) >= glm::length(moonRad)) { L.lightDir = L.sunDir; L.lightColor = sunRad; }
    else                                              { L.lightDir = L.moonDir; L.lightColor = moonRad; }

    L.lamp = 1.0f - glm::smoothstep(-0.04f, 0.10f, sy);
    L.exposure = glm::mix(2.6f, 0.9f, glm::smoothstep(-0.18f, 0.12f, sy));
    return L;
}

// ---------------------------------------------------------------------------
// Scene description
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// Boat path: a quadratic Bezier from far out at sea to a berth beside the jetty.
// The boat cruises, slows down for the last 25% of the trip, docks, waits, repeats.
// ---------------------------------------------------------------------------
struct BoatPath {
    glm::vec2 P0, P1, P2;
    float sailTime = 80.0f, waitTime = 14.0f;
};

struct BoatPose {
    glm::vec3 pos;
    float yaw;       // rotation about Y (boat +X = bow)
    float speed;     // m/s, drives the wake
    glm::vec2 fwd;   // world-space heading (x,z)
};

static BoatPose boatPoseAt(const BoatPath& bp, float animT) {
    float total = bp.sailTime + bp.waitTime;
    float cyc = std::fmod(animT, total);
    float x = std::clamp(cyc / bp.sailTime, 0.0f, 1.0f);
    float posF, spdF;
    if (x <= 0.75f) { posF = x; spdF = 1.0f; }
    else {
        float d = x - 0.75f;
        posF = 0.75f + d - d * d / 0.5f;
        spdF = 1.0f - d / 0.25f;
    }
    float u = posF / 0.875f;
    float us = spdF / 0.875f;
    glm::vec2 P = (1 - u) * (1 - u) * bp.P0 + 2 * (1 - u) * u * bp.P1 + u * u * bp.P2;
    glm::vec2 dP = 2.0f * (1 - u) * (bp.P1 - bp.P0) + 2.0f * u * (bp.P2 - bp.P1);
    glm::vec2 fwd = glm::normalize(dP);
    BoatPose r;
    r.pos = glm::vec3(P.x, 0.0f, P.y);
    r.yaw = std::atan2(-fwd.y, fwd.x);
    r.speed = glm::length(dP) * us / bp.sailTime;
    r.fwd = fwd;
    return r;
}

struct Item {
    Mesh* mesh;
    glm::mat4 model;
    int material;
    float emissive;
    bool castShadow;
};

int main(int argc, char** argv) {
    int winW = 1600, winH = 900;
    std::string shotPath;
    float fixedTime = -1.0f;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](float& dst) { if (i + 1 < argc) dst = (float)std::atof(argv[++i]); };
        if (a == "--shot" && i + 1 < argc) shotPath = argv[++i];
        else if (a == "--time") next(gTime);
        else if (a == "--yaw") next(gYaw);
        else if (a == "--pitch") next(gPitch);
        else if (a == "--dist") next(gDist);
        else if (a == "--w" && i + 1 < argc) winW = std::atoi(argv[++i]);
        else if (a == "--h" && i + 1 < argc) winH = std::atoi(argv[++i]);
        else if (a == "--wave") next(gWaveAmp);
        else if (a == "--anim") next(gAnimT);
        else if (a == "--beam") next(gBeamAngle);
        else if (a == "--lamp") gForceLamp = true;
        else if (a == "--wire") gWire = true;
    }
    bool headless = !shotPath.empty();
    if (headless) fixedTime = 12.34f;     // deterministic water/clouds for screenshots

    if (!glfwInit()) { std::fprintf(stderr, "glfwInit failed\n"); return 1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    GLFWwindow* win = glfwCreateWindow(winW, winH, "Track B: Lighthouse - Stage B2 (Realistic Look)", nullptr, nullptr);
    if (!win) { std::fprintf(stderr, "Window creation failed\n"); return 1; }
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);
    if (!LOAD_GL()) { std::fprintf(stderr, "Failed to load OpenGL\n"); return 1; }
    std::printf("GL: %s | %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));

    glfwSetKeyCallback(win, onKey);
    glfwSetMouseButtonCallback(win, onMouseButton);
    glfwSetCursorPosCallback(win, onCursor);
    glfwSetScrollCallback(win, onScroll);

    // ---- programs ----
    const std::string V = GLSL_VERSION;
    Program pSky    = makeProgram(V + SKY_VS, V + GLSL_COMMON + SKY_FS, "sky");
    Program pShadow = makeProgram(V + SHADOW_VS, V + SHADOW_FS, "shadow");
    Program pScene  = makeProgram(V + SCENE_VS, V + GLSL_COMMON + GLSL_BEAM + SCENE_FS, "scene");
    Program pWater  = makeProgram(V + WATER_VS, V + GLSL_COMMON + GLSL_BEAM + GLSL_ISLAND + WATER_FS, "water");
    Program pBeam   = makeProgram(V + POST_VS, V + GLSL_BEAM + BEAM_FS, "beam");
    Program pExtract = makeProgram(V + POST_VS, V + EXTRACT_FS, "extract");
    Program pCopy    = makeProgram(V + POST_VS, V + COPY_FS, "copy");
    Program pBlur    = makeProgram(V + POST_VS, V + BLUR_FS, "blur");
    Program pComp    = makeProgram(V + POST_VS, V + COMPOSITE_FS, "composite");

    // ---- geometry ----
    Mesh terrain, towerM, metalM, glassM, roofM, boatBody, boatCabin, boatGlass, boatLights, person, waterQuad, jettyDeck, jettyPosts;
    std::vector<Mesh> rocks(3);
    terrain.upload(makeTerrain());
    LighthouseMeshes lh = makeLighthouse();
    towerM.upload(lh.tower); metalM.upload(lh.metal); glassM.upload(lh.glass); roofM.upload(lh.roof);
    BoatMeshes bt = makeBoat();
    boatBody.upload(bt.body); boatCabin.upload(bt.cabin); boatGlass.upload(bt.glass); person.upload(bt.person);
    boatLights.upload(bt.lights);
    const float jettyAngle = -0.45f;
    JettyMeshes jt = makeJetty(jettyAngle);
    jettyDeck.upload(jt.deck); jettyPosts.upload(jt.posts);
    for (int k = 0; k < 3; ++k) rocks[k].upload(makeRock(3.1f + 7.7f * k));
    {
        MeshData q;
        const float S = 5000.0f;
        glm::vec3 c[4] = {{-S, 0, -S}, {S, 0, -S}, {S, 0, S}, {-S, 0, S}};
        for (auto& p : c) q.v.push_back({p, {0, 1, 0}, {0, 0}, {1, 1, 1}});
        q.i = {0, 3, 2, 0, 2, 1};
        waterQuad.upload(q);
    }

    // ---- scene items ----
    std::vector<Item> items;
    const glm::mat4 I(1.0f);
    items.push_back({&terrain, I, 4, 0, true});
    items.push_back({&towerM, I, 0, 0, true});
    items.push_back({&metalM, I, 1, 0, true});
    items.push_back({&glassM, I, 2, 7.0f, true});
    items.push_back({&roofM, I, 3, 0, true});

    // jetty
    items.push_back({&jettyDeck, I, 6, 0, true});
    items.push_back({&jettyPosts, I, 7, 0, true});

    // boat: berth alongside the seaward end of the jetty, approach path from the open sea
    glm::vec2 jdir = jt.dir, jperp(-jt.dir.y, jt.dir.x);
    BoatPath path;
    path.P2 = jdir * (jt.rEnd - 2.5f) - jperp * 2.1f;      // berth
    path.P1 = path.P2 + jdir * 28.0f;                       // final approach runs shoreward along the jetty
    path.P0 = glm::vec2(5.0f, -95.0f);                      // far out to the right of the view
    int boatFirst = (int)items.size();
    items.push_back({&boatBody, I, 6, 0, true});
    items.push_back({&boatCabin, I, 7, 0, true});
    items.push_back({&boatGlass, I, 2, 1.6f, true});
    items.push_back({&boatLights, I, 8, 14.0f, true});
    items.push_back({&person, I, 7, 0, true});
    int boatLast = (int)items.size();

    // rocks scattered along the shoreline
    {
        uint32_t seed = 12345;
        auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return (seed >> 8) / 16777216.0f; };
        for (int k = 0; k < 24; ++k) {
            float a = rnd() * kTwoPi;
            float da = std::atan2(std::sin(a - jettyAngle), std::cos(a - jettyAngle));
            if (std::fabs(da) < 0.30f) continue;
            float r = 8.0f;
            while (islandHeight(r * std::cos(a), r * std::sin(a)) > 0.3f && r < 60.0f) r += 0.1f;
            r += (rnd() - 0.5f) * 3.0f;
            float s = 0.7f + rnd() * 1.9f;
            glm::vec3 p(r * std::cos(a), islandHeight(r * std::cos(a), r * std::sin(a)) - 0.15f * s, r * std::sin(a));
            glm::mat4 M = glm::translate(I, p) * glm::rotate(I, rnd() * kTwoPi, glm::vec3(0, 1, 0)) *
                          glm::scale(I, glm::vec3(s));
            items.push_back({&rocks[k % 3], M, 5, 0, true});
        }
    }

    // ---- targets ----
    Targets T;
    createShadowTarget(T);
    GLuint emptyVao;
    glGenVertexArrays(1, &emptyVao);

    auto drawItems = [&](Program& p, bool shadowPass) {
        for (const Item& it : items) {
            if (shadowPass && !it.castShadow) continue;
            p.m4("uModel", it.model);
            if (!shadowPass) {
                p.i1("uMaterial", it.material);
                p.f1("uEmissive", it.emissive);
            }
            it.mesh->draw();
        }
    };
    auto fullscreen = [&]() { glBindVertexArray(emptyVao); glDrawArrays(GL_TRIANGLES, 0, 3); };

    double lastT = glfwGetTime();
    int frames = 0;

    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();
        double now = glfwGetTime();
        float dt = (float)(now - lastT);
        lastT = now;

        if (glfwGetKey(win, GLFW_KEY_LEFT) == GLFW_PRESS) gYaw += 0.9f * dt;
        if (glfwGetKey(win, GLFW_KEY_RIGHT) == GLFW_PRESS) gYaw -= 0.9f * dt;
        if (glfwGetKey(win, GLFW_KEY_UP) == GLFW_PRESS) gPitch = std::min(1.35f, gPitch + 0.6f * dt);
        if (glfwGetKey(win, GLFW_KEY_DOWN) == GLFW_PRESS) gPitch = std::max(-0.2f, gPitch - 0.6f * dt);
        if (gAutoTime) gTime = std::fmod(gTime + dt * 0.5f, 24.0f);   // 1 game hour per 2 s
        if (!gPaused && !headless) {
            gAnimT += dt * (gFast ? 4.0f : 1.0f);
            gBeamAngle += dt * kTwoPi / gBeamPeriod;               // clockwise seen from above
            if (gBeamAngle > kTwoPi) gBeamAngle -= kTwoPi;
        }

        int W, H;
        glfwGetFramebufferSize(win, &W, &H);
        if (W < 2 || H < 2) { glfwSwapBuffers(win); continue; }
        if (W != T.w || H != T.h) createScreenTargets(T, W, H);

        float time = fixedTime >= 0 ? fixedTime : (float)now;
        Lighting Lt = computeLighting(gTime);
        float lampOn = gForceLamp ? 1.0f : Lt.lamp;
        Lt.lamp = lampOn;

        // ---- animate the boat ----
        BoatPose bp = boatPoseAt(path, gAnimT);
        {
            float t = gAnimT;
            float heave = 0.05f * std::sin(1.1f * t) + 0.03f * std::sin(1.9f * t + 1.0f);
            float roll = 0.035f * std::sin(0.9f * t + 0.5f);
            float pitch = 0.025f * std::sin(1.3f * t);
            glm::mat4 bm = glm::translate(I, bp.pos + glm::vec3(0, heave, 0)) *
                           glm::rotate(I, bp.yaw, glm::vec3(0, 1, 0)) *
                           glm::rotate(I, roll, glm::vec3(1, 0, 0)) *
                           glm::rotate(I, pitch, glm::vec3(0, 0, 1));
            for (int k = boatFirst; k < boatLast; ++k) items[k].model = bm;
        }
        glm::vec3 lampPos(0.0f, lh.lampY, 0.0f);
        if (headless && frames == 0) std::printf("boat x=%.2f z=%.2f azimuth=%.3f rad\n", bp.pos.x, bp.pos.z, std::atan2(bp.pos.z, bp.pos.x));

        // ---- camera ----
        glm::vec3 target = gFollow ? bp.pos + glm::vec3(0, 2.0f, 0) : glm::vec3(0.0f, 11.0f, 0.0f);
        glm::vec3 camPos = target + gDist * glm::vec3(std::cos(gPitch) * std::sin(gYaw), std::sin(gPitch),
                                                      std::cos(gPitch) * std::cos(gYaw));
        camPos.y = std::max(camPos.y, 1.0f);
        glm::mat4 view = glm::lookAt(camPos, target, glm::vec3(0, 1, 0));
        glm::mat4 proj = glm::perspective(glm::radians(48.0f), (float)W / H, 0.3f, 8000.0f);
        glm::mat4 viewProj = proj * view;
        glm::mat4 mirror = glm::scale(I, glm::vec3(1, -1, 1));
        glm::mat4 reflViewProj = proj * view * mirror;
        glm::vec3 reflCamPos(camPos.x, -camPos.y, camPos.z);

        // ---- light camera (orthographic, covers the island and the boat) ----
        glm::vec3 center(0, 6, 0);
        glm::vec3 ld = glm::normalize(Lt.lightDir);
        glm::vec3 upv = std::fabs(ld.y) > 0.99f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
        glm::mat4 lightV = glm::lookAt(center + ld * 120.0f, center, upv);
        glm::mat4 lightP = glm::ortho(-48.0f, 48.0f, -48.0f, 48.0f, 1.0f, 260.0f);
        glm::mat4 lightVP = lightP * lightV;

        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glFrontFace(GL_CCW);
        glDisable(GL_BLEND);

        // ================= 1. shadow pass =================
        glBindFramebuffer(GL_FRAMEBUFFER, T.shadowFbo);
        glViewport(0, 0, T.shadowSize, T.shadowSize);
        glClear(GL_DEPTH_BUFFER_BIT);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(2.0f, 4.0f);
        pShadow.use();
        pShadow.m4("uLightVP", lightVP);
        drawItems(pShadow, true);
        glDisable(GL_POLYGON_OFFSET_FILL);

        auto setSceneUniforms = [&](const glm::mat4& vp, const glm::vec3& cp) {
            pScene.use();
            pScene.m4("uViewProj", vp);
            pScene.f3("uCamPos", cp);
            pScene.f3("uSunDir", Lt.sunDir);
            pScene.f3("uMoonDir", Lt.moonDir);
            pScene.f1("uTime", time);
            pScene.f3("uLightDir", Lt.lightDir);
            pScene.f3("uLightColor", Lt.lightColor);
            pScene.m4("uLightVP", lightVP);
            pScene.f1("uLamp", Lt.lamp);
            pScene.f1("uFogDensity", 0.0009f);
            pScene.i1("uShadowMap", 0);
            pScene.f1("uBeamAngle", gBeamAngle);
            pScene.f3("uLampPos", lampPos);
            pScene.f1("uBeamOn", lampOn);
        };
        auto drawSky = [&](const glm::mat4& vp, const glm::vec3& cp) {
            pSky.use();
            pSky.m4("uInvViewProj", glm::inverse(vp));
            pSky.f3("uCamPos", cp);
            pSky.f3("uSunDir", Lt.sunDir);
            pSky.f3("uMoonDir", Lt.moonDir);
            pSky.f1("uTime", time);
            glDisable(GL_DEPTH_TEST);
            fullscreen();
            glEnable(GL_DEPTH_TEST);
        };

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, T.shadowTex);

        // ================= 2. reflection pass =================
        glBindFramebuffer(GL_FRAMEBUFFER, T.reflFbo);
        glViewport(0, 0, W, H);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        drawSky(reflViewProj, reflCamPos);
        glFrontFace(GL_CW);                       // mirroring flips triangle winding
        glEnable(GL_CLIP_DISTANCE0);
        setSceneUniforms(reflViewProj, reflCamPos);
        drawItems(pScene, false);
        glDisable(GL_CLIP_DISTANCE0);
        glFrontFace(GL_CCW);

        // ================= 3. main HDR pass (MSAA) =================
        glBindFramebuffer(GL_FRAMEBUFFER, T.msFbo);
        glViewport(0, 0, W, H);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        drawSky(viewProj, camPos);
        if (gWire) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        setSceneUniforms(viewProj, camPos);
        drawItems(pScene, false);

        // water (alpha-blended over the sea bed so the shallows are see-through)
        glDisable(GL_CULL_FACE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        pWater.use();
        pWater.m4("uViewProj", viewProj);
        pWater.f3("uCamPos", camPos);
        pWater.f2("uResolution", glm::vec2((float)W, (float)H));
        pWater.f3("uSunDir", Lt.sunDir);
        pWater.f3("uMoonDir", Lt.moonDir);
        pWater.f1("uTime", time);
        pWater.f3("uLightDir", Lt.lightDir);
        pWater.f3("uLightColor", Lt.lightColor);
        pWater.f1("uWaveAmp", gWaveAmp);
        pWater.f1("uFogDensity", 0.0009f);
        pWater.i1("uReflection", 1);
        pWater.f1("uBeamAngle", gBeamAngle);
        pWater.f3("uLampPos", lampPos);
        pWater.f1("uBeamOn", lampOn);
        pWater.f2("uBoatPos", glm::vec2(bp.pos.x, bp.pos.z));
        pWater.f2("uBoatFwd", bp.fwd);
        pWater.f1("uBoatSpeed", bp.speed);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, T.reflTex);
        waterQuad.draw();
        glDisable(GL_BLEND);
        glEnable(GL_CULL_FACE);
        if (gWire) glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

        // resolve MSAA -> HDR texture
        glBindFramebuffer(GL_READ_FRAMEBUFFER, T.msFbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, T.hdrFbo);
        glBlitFramebuffer(0, 0, W, H, 0, 0, W, H, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, T.depthFbo);
        glBlitFramebuffer(0, 0, W, H, 0, 0, W, H, GL_DEPTH_BUFFER_BIT, GL_NEAREST);

        // ================= 4. volumetric light shaft (additive onto the HDR image) =================
        if (lampOn > 0.01f) {
            glBindFramebuffer(GL_FRAMEBUFFER, T.hdrFbo);
            glViewport(0, 0, W, H);
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_CULL_FACE);
            glEnable(GL_BLEND);
            glBlendFunc(GL_ONE, GL_ONE);
            pBeam.use();
            pBeam.m4("uInvViewProj", glm::inverse(viewProj));
            pBeam.f3("uCamPos", camPos);
            pBeam.f1("uTime", time);
            pBeam.f1("uHaze", 0.002f);
            pBeam.f1("uBeamAngle", gBeamAngle);
            pBeam.f3("uLampPos", lampPos);
            pBeam.f1("uBeamOn", lampOn);
            pBeam.i1("uDepth", 0);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, T.depthTex);
            fullscreen();
            glDisable(GL_BLEND);
        }

        // ================= 5. bloom =================
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glActiveTexture(GL_TEXTURE0);
        for (int i = 0; i < 4; ++i) {
            BloomLevel& b = T.bloom[i];
            glViewport(0, 0, b.w, b.h);
            glBindFramebuffer(GL_FRAMEBUFFER, b.fbo[0]);
            if (i == 0) {
                pExtract.use();
                pExtract.i1("uTex", 0);
                pExtract.f1("uExposure", Lt.exposure);
                glBindTexture(GL_TEXTURE_2D, T.hdrTex);
            } else {
                pCopy.use();
                pCopy.i1("uTex", 0);
                glBindTexture(GL_TEXTURE_2D, T.bloom[i - 1].tex[0]);
            }
            fullscreen();

            pBlur.use();
            pBlur.i1("uTex", 0);
            glBindFramebuffer(GL_FRAMEBUFFER, b.fbo[1]);
            glBindTexture(GL_TEXTURE_2D, b.tex[0]);
            pBlur.f2("uDir", glm::vec2(1.0f / b.w, 0.0f));
            fullscreen();
            glBindFramebuffer(GL_FRAMEBUFFER, b.fbo[0]);
            glBindTexture(GL_TEXTURE_2D, b.tex[1]);
            pBlur.f2("uDir", glm::vec2(0.0f, 1.0f / b.h));
            fullscreen();
        }

        // ================= 6. tonemap + gamma to the window =================
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, W, H);
        pComp.use();
        pComp.f1("uExposure", Lt.exposure);
        pComp.f1("uBloomStrength", 0.65f);
        const char* names[5] = {"uScene", "uBloom0", "uBloom1", "uBloom2", "uBloom3"};
        GLuint texs[5] = {T.hdrTex, T.bloom[0].tex[0], T.bloom[1].tex[0], T.bloom[2].tex[0], T.bloom[3].tex[0]};
        for (int k = 0; k < 5; ++k) {
            glActiveTexture(GL_TEXTURE0 + k);
            glBindTexture(GL_TEXTURE_2D, texs[k]);
            pComp.i1(names[k], k);
        }
        fullscreen();
        glActiveTexture(GL_TEXTURE0);

        // ---- screenshot ----
        bool wantShot = gShotRequested || (headless && frames == 2);
        if (wantShot) {
            std::vector<unsigned char> px((size_t)W * H * 3);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadBuffer(GL_BACK);
            glReadPixels(0, 0, W, H, GL_RGB, GL_UNSIGNED_BYTE, px.data());
            std::string path = headless ? shotPath : "screenshot_" + std::to_string((int)(now * 10)) + ".png";
            stbi_flip_vertically_on_write(1);
            stbi_write_png(path.c_str(), W, H, 3, px.data(), W * 3);
            std::printf("Saved %s\n", path.c_str());
            gShotRequested = false;
            if (headless) break;
        }

        glfwSwapBuffers(win);
        ++frames;
    }
    glfwTerminate();
    return 0;
}