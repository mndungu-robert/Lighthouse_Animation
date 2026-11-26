// geometry.h  -  CPU-side mesh builders for the lighthouse scene (Stage B2)
// Everything is built from simple maths: lathes (surfaces of revolution), boxes,
// parametric surfaces and a hand-lofted boat hull. No external models needed.
#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <functional>
#include <cstdint>
#include <cmath>
#include <algorithm>

constexpr float kPi = 3.14159265358979f;
constexpr float kTwoPi = 6.28318530717959f;

struct Vertex {
    glm::vec3 pos;
    glm::vec3 nrm;
    glm::vec2 uv;
    glm::vec3 col;
};

struct MeshData {
    std::vector<Vertex> v;
    std::vector<uint32_t> i;
};

// ---------------------------------------------------------------------------
// Island height.  MUST stay identical to islandHeight() in shaders.h, because
// the water shader uses it to work out water depth and shoreline foam.
// ---------------------------------------------------------------------------
inline float islandHeight(float x, float z) {
    float r = std::sqrt(x * x + z * z);
    float a = std::atan2(z, x);
    float wob = 1.0f + 0.10f * std::sin(3.0f * a + 0.7f)
                     + 0.06f * std::sin(5.0f * a + 2.1f)
                     + 0.04f * std::sin(9.0f * a + 4.0f);
    float s = (r / wob) / 18.0f;
    return 9.5f * std::exp(-s * s) - 3.5f;
}

// ---------------------------------------------------------------------------
// Small hash / value noise (used for rock shapes)
// ---------------------------------------------------------------------------
inline float hash31(glm::vec3 p) {
    p = glm::fract(p * 0.1031f);
    p += glm::dot(p, glm::vec3(p.z, p.y, p.x) + 31.32f);
    return glm::fract((p.x + p.y) * p.z);
}

inline float vnoise3(const glm::vec3& p) {
    glm::vec3 i = glm::floor(p);
    glm::vec3 f = p - i;
    f = f * f * (3.0f - 2.0f * f);
    auto h = [&](float x, float y, float z) { return hash31(i + glm::vec3(x, y, z)); };
    float a = glm::mix(h(0, 0, 0), h(1, 0, 0), f.x);
    float b = glm::mix(h(0, 1, 0), h(1, 1, 0), f.x);
    float c = glm::mix(h(0, 0, 1), h(1, 0, 1), f.x);
    float d = glm::mix(h(0, 1, 1), h(1, 1, 1), f.x);
    return glm::mix(glm::mix(a, b, f.y), glm::mix(c, d, f.y), f.z);
}

// ---------------------------------------------------------------------------
// Low-level helpers
// ---------------------------------------------------------------------------
inline glm::mat3 normalMatrix(const glm::mat4& T) {
    return glm::transpose(glm::inverse(glm::mat3(T)));
}

inline void pushVertex(MeshData& m, const glm::mat4& T, const glm::mat3& NM,
                       const glm::vec3& p, const glm::vec3& n,
                       const glm::vec2& uv, const glm::vec3& col) {
    Vertex v;
    v.pos = glm::vec3(T * glm::vec4(p, 1.0f));
    glm::vec3 nn = NM * n;
    float len = glm::length(nn);
    v.nrm = len > 1e-12f ? nn / len : glm::vec3(0, 1, 0);
    v.uv = uv;
    v.col = col;
    m.v.push_back(v);
}

// ---------------------------------------------------------------------------
// Lathe: revolve a 2D profile (radius, height) around the Y axis.
// Each call is ONE smooth-shaded polyline. For a hard edge, call it again with
// a separate polyline. Triangles are wound CCW seen from outside.
// uv = (angle 0..1, profile height in metres)
// ---------------------------------------------------------------------------
struct P2 { float r, y; };

inline void addLathe(MeshData& m, const std::vector<P2>& pr, int seg,
                     const glm::vec3& col, const glm::mat4& T = glm::mat4(1.0f)) {
    int n = (int)pr.size();
    if (n < 2) return;
    glm::mat3 NM = normalMatrix(T);

    std::vector<glm::vec2> segN(n - 1), ptN(n);
    for (int k = 0; k < n - 1; ++k) {
        glm::vec2 t(pr[k + 1].r - pr[k].r, pr[k + 1].y - pr[k].y);
        glm::vec2 nn(t.y, -t.x);
        float len = glm::length(nn);
        segN[k] = len > 1e-8f ? nn / len : glm::vec2(1, 0);
    }
    for (int k = 0; k < n; ++k) {
        if (k == 0) ptN[k] = segN[0];
        else if (k == n - 1) ptN[k] = segN[n - 2];
        else {
            glm::vec2 a = segN[k - 1] + segN[k];
            float len = glm::length(a);
            ptN[k] = len > 1e-6f ? a / len : segN[k];
        }
    }

    uint32_t base = (uint32_t)m.v.size();
    for (int k = 0; k < n; ++k) {
        for (int j = 0; j <= seg; ++j) {
            float u = (float)j / seg;
            float a = u * kTwoPi;
            float c = std::cos(a), s = std::sin(a);
            pushVertex(m, T, NM,
                       glm::vec3(pr[k].r * c, pr[k].y, pr[k].r * s),
                       glm::vec3(ptN[k].x * c, ptN[k].y, ptN[k].x * s),
                       glm::vec2(u, pr[k].y), col);
        }
    }
    int stride = seg + 1;
    for (int k = 0; k < n - 1; ++k) {
        for (int j = 0; j < seg; ++j) {
            uint32_t a = base + k * stride + j;
            uint32_t b = a + 1;
            uint32_t c = a + stride;
            uint32_t d = c + 1;
            m.i.insert(m.i.end(), {a, c, b, b, c, d});
        }
    }
}

// Cylinder along +Y starting at the origin of T
inline void addCylinder(MeshData& m, float r0, float r1, float L, int seg,
                        const glm::vec3& col, const glm::mat4& T, bool caps = true) {
    addLathe(m, {{r0, 0.0f}, {r1, L}}, seg, col, T);
    if (caps) {
        addLathe(m, {{0.0f, 0.0f}, {r0, 0.0f}}, seg, col, T);
        addLathe(m, {{r1, L}, {0.0f, L}}, seg, col, T);
    }
}

// Basis whose +Y axis points from a to b
inline glm::mat4 alignY(const glm::vec3& a, const glm::vec3& b, float& len) {
    glm::vec3 d = b - a;
    len = glm::length(d);
    glm::vec3 y = d / std::max(len, 1e-8f);
    glm::vec3 helper = std::fabs(y.y) < 0.99f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
    glm::vec3 x = glm::normalize(glm::cross(helper, y));
    glm::vec3 z = glm::cross(x, y);
    glm::mat4 M(1.0f);
    M[0] = glm::vec4(x, 0);
    M[1] = glm::vec4(y, 0);
    M[2] = glm::vec4(z, 0);
    M[3] = glm::vec4(a, 1);
    return M;
}

inline void addCylinderBetween(MeshData& m, const glm::vec3& a, const glm::vec3& b,
                               float r0, float r1, int seg, const glm::vec3& col,
                               const glm::mat4& T = glm::mat4(1.0f)) {
    float len;
    glm::mat4 M = alignY(a, b, len);
    addCylinder(m, r0, r1, len, seg, col, T * M, true);
}

// ---------------------------------------------------------------------------
// Box with hard edges (24 vertices). uv in metres.
// ---------------------------------------------------------------------------
inline void addBox(MeshData& m, const glm::vec3& half, const glm::vec3& col,
                   const glm::mat4& T = glm::mat4(1.0f)) {
    struct Face { glm::vec3 n, u, v; };
    static const Face faces[6] = {
        {{1, 0, 0},  {0, 0, -1}, {0, 1, 0}},
        {{-1, 0, 0}, {0, 0, 1},  {0, 1, 0}},
        {{0, 1, 0},  {1, 0, 0},  {0, 0, -1}},
        {{0, -1, 0}, {1, 0, 0},  {0, 0, 1}},
        {{0, 0, 1},  {1, 0, 0},  {0, 1, 0}},
        {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}},
    };
    glm::mat3 NM = normalMatrix(T);
    for (const Face& f : faces) {
        auto ext = [&](const glm::vec3& a) {
            return std::fabs(a.x) * half.x + std::fabs(a.y) * half.y + std::fabs(a.z) * half.z;
        };
        float hu = ext(f.u), hv = ext(f.v), hn = ext(f.n);
        glm::vec3 c = f.n * hn;
        uint32_t base = (uint32_t)m.v.size();
        pushVertex(m, T, NM, c - f.u * hu - f.v * hv, f.n, {0, 0}, col);
        pushVertex(m, T, NM, c + f.u * hu - f.v * hv, f.n, {2 * hu, 0}, col);
        pushVertex(m, T, NM, c + f.u * hu + f.v * hv, f.n, {2 * hu, 2 * hv}, col);
        pushVertex(m, T, NM, c - f.u * hu + f.v * hv, f.n, {0, 2 * hv}, col);
        m.i.insert(m.i.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
}

// ---------------------------------------------------------------------------
// Parametric surface f(u,v), u,v in [0,1].  Normals come from the numerical
// cross product dF/du x dF/dv, so seams and curved shapes shade smoothly.
// flip=false -> faces point along dF/du x dF/dv.
// ---------------------------------------------------------------------------
using SurfFn = std::function<glm::vec3(float, float)>;
using UVFn = std::function<glm::vec2(float, float)>;

inline void addParamSurface(MeshData& m, int nu, int nv, const SurfFn& f,
                            const glm::vec3& col, bool flip,
                            const glm::mat4& T = glm::mat4(1.0f),
                            const UVFn& uvf = nullptr) {
    glm::mat3 NM = normalMatrix(T);
    uint32_t base = (uint32_t)m.v.size();
    const float e = 1e-3f;
    for (int i = 0; i <= nu; ++i) {
        for (int j = 0; j <= nv; ++j) {
            float u = (float)i / nu, v = (float)j / nv;
            glm::vec3 p = f(u, v);
            float u0 = std::max(u - e, 0.0f), u1 = std::min(u + e, 1.0f);
            float v0 = std::max(v - e, 0.0f), v1 = std::min(v + e, 1.0f);
            glm::vec3 du = f(u1, v) - f(u0, v);
            glm::vec3 dv = f(u, v1) - f(u, v0);
            glm::vec3 n = glm::cross(du, dv);
            float len = glm::length(n);
            n = len > 1e-12f ? n / len : glm::vec3(0, 1, 0);
            if (flip) n = -n;
            glm::vec2 uv = uvf ? uvf(u, v) : glm::vec2(u, v);
            pushVertex(m, T, NM, p, n, uv, col);
        }
    }
    int stride = nv + 1;
    for (int i = 0; i < nu; ++i) {
        for (int j = 0; j < nv; ++j) {
            uint32_t a = base + i * stride + j;
            uint32_t b = a + stride;
            uint32_t c = a + 1;
            uint32_t d = b + 1;
            if (!flip) m.i.insert(m.i.end(), {a, b, c, b, d, c});
            else       m.i.insert(m.i.end(), {a, c, b, b, c, d});
        }
    }
}

inline void addEllipsoid(MeshData& m, const glm::vec3& r, int seg, int rings,
                         const glm::vec3& col, const glm::mat4& T) {
    addParamSurface(m, seg, rings, [=](float u, float v) {
        float ph = u * kTwoPi, th = v * kPi;
        return glm::vec3(r.x * std::sin(th) * std::cos(ph), r.y * std::cos(th),
                         r.z * std::sin(th) * std::sin(ph));
    }, col, false, T);
}

// ---------------------------------------------------------------------------
// Terrain: polar grid (dense near the centre), heights from islandHeight().
// ---------------------------------------------------------------------------
inline MeshData makeTerrain(float R = 90.0f, int NR = 170, int NA = 360) {
    MeshData m;
    const float eps = 0.05f;
    for (int i = 0; i <= NR; ++i) {
        float r = R * std::pow((float)i / NR, 1.8f);
        for (int j = 0; j <= NA; ++j) {
            float a = kTwoPi * (float)j / NA;
            float x = r * std::cos(a), z = r * std::sin(a);
            float h = islandHeight(x, z);
            float dx = (islandHeight(x + eps, z) - islandHeight(x - eps, z)) / (2 * eps);
            float dz = (islandHeight(x, z + eps) - islandHeight(x, z - eps)) / (2 * eps);
            Vertex v;
            v.pos = {x, h, z};
            v.nrm = glm::normalize(glm::vec3(-dx, 1.0f, -dz));
            v.uv = {x, z};
            v.col = {1, 1, 1};
            m.v.push_back(v);
        }
    }
    int stride = NA + 1;
    for (int i = 0; i < NR; ++i) {
        for (int j = 0; j < NA; ++j) {
            uint32_t a = i * stride + j, b = a + 1, c = a + stride, d = c + 1;
            m.i.insert(m.i.end(), {a, b, c, b, d, c});
        }
    }
    return m;
}

// ---------------------------------------------------------------------------
// Rock: displaced ellipsoid
// ---------------------------------------------------------------------------
inline MeshData makeRock(float seed) {
    MeshData m;
    addParamSurface(m, 30, 20, [=](float u, float v) {
        float ph = u * kTwoPi, th = v * kPi;
        glm::vec3 d(std::sin(th) * std::cos(ph), std::cos(th), std::sin(th) * std::sin(ph));
        float n = vnoise3(d * 1.7f + glm::vec3(seed)) * 0.60f
                + vnoise3(d * 4.1f + glm::vec3(seed * 1.7f)) * 0.28f
                + vnoise3(d * 9.0f + glm::vec3(seed * 2.3f)) * 0.12f;
        float rad = 0.62f + 0.75f * n;
        glm::vec3 p = d * rad;
        p.y *= 0.70f;
        return p;
    }, glm::vec3(1.0f), false);
    return m;
}

// ---------------------------------------------------------------------------
// Lighthouse (world coordinates, standing on top of the island)
// uv.y of the tower = height in metres above its base (used by the stripe shader)
// ---------------------------------------------------------------------------
struct LighthouseMeshes {
    MeshData tower;   // material 0: striped plaster, door, windows
    MeshData metal;   // material 1: gallery, rails, frames
    MeshData glass;   // material 2: lantern glass (emissive at night)
    MeshData roof;    // material 3: roof + finial
    float baseY = 0.0f;
    float lampY = 0.0f;   // height of the lamp above sea level (for the beam later)
};

inline LighthouseMeshes makeLighthouse() {
    LighthouseMeshes L;
    L.baseY = islandHeight(0, 0) - 0.4f;
    const glm::mat4 T = glm::translate(glm::mat4(1.0f), glm::vec3(0, L.baseY, 0));
    const glm::vec3 white(0.85f), dark(0.06f, 0.06f, 0.07f), redRoof(0.35f, 0.04f, 0.03f);
    const int S = 64;

    // --- tower ---
    addLathe(L.tower, {{3.5f, 0.0f}, {3.5f, 1.3f}}, S, white, T);          // plinth side
    addLathe(L.tower, {{3.5f, 1.3f}, {2.85f, 1.3f}}, S, white, T);         // plinth top
    addLathe(L.tower, {{2.85f, 1.3f}, {1.85f, 20.6f}}, S, white, T);       // tapered body

    // --- gallery ---
    addLathe(L.metal, {{1.95f, 20.6f}, {3.15f, 21.35f}}, S, dark, T);
    addLathe(L.metal, {{3.15f, 21.35f}, {3.15f, 21.65f}}, S, dark, T);
    addLathe(L.metal, {{3.15f, 21.65f}, {1.45f, 21.65f}}, S, dark, T);
    // lantern base ring and cornice
    addLathe(L.metal, {{1.45f, 21.65f}, {1.45f, 22.05f}}, S, dark, T);
    addLathe(L.metal, {{1.45f, 22.05f}, {1.3f, 22.05f}}, S, dark, T);
    addLathe(L.metal, {{1.3f, 24.15f}, {1.5f, 24.15f}}, S, dark, T);
    addLathe(L.metal, {{1.5f, 24.15f}, {1.5f, 24.45f}}, S, dark, T);
    // rail posts + rails
    const int posts = 28;
    const float rr = 3.0f;
    for (int k = 0; k < posts; ++k) {
        float a = kTwoPi * k / posts;
        addCylinder(L.metal, 0.035f, 0.035f, 1.15f, 6, dark,
                    T * glm::translate(glm::mat4(1.0f), glm::vec3(rr * std::cos(a), 21.65f, rr * std::sin(a))), false);
        float a2 = kTwoPi * (k + 1) / posts;
        glm::vec3 p0(rr * std::cos(a), 0, rr * std::sin(a)), p1(rr * std::cos(a2), 0, rr * std::sin(a2));
        glm::vec3 mid = 0.5f * (p0 + p1);
        glm::vec3 d = p1 - p0;
        float len = glm::length(d);
        float th = std::atan2(-d.z, d.x);
        for (float ry : {22.8f, 22.2f}) {
            glm::mat4 B = glm::translate(glm::mat4(1.0f), glm::vec3(mid.x, ry, mid.z)) *
                          glm::rotate(glm::mat4(1.0f), th, glm::vec3(0, 1, 0));
            addBox(L.metal, glm::vec3(len * 0.5f, 0.03f, 0.03f), dark, T * B);
        }
    }
    // glazing bars
    for (int k = 0; k < 12; ++k) {
        float a = kTwoPi * k / 12.0f;
        glm::mat4 B = glm::translate(glm::mat4(1.0f), glm::vec3(1.29f * std::cos(a), 23.1f, 1.29f * std::sin(a))) *
                      glm::rotate(glm::mat4(1.0f), -a, glm::vec3(0, 1, 0));
        addBox(L.metal, glm::vec3(0.03f, 1.05f, 0.05f), dark, T * B);
    }

    // --- lantern glass ---
    addLathe(L.glass, {{1.3f, 22.05f}, {1.3f, 24.15f}}, S, glm::vec3(0.9f), T);

    // --- roof ---
    addLathe(L.roof, {{1.5f, 24.45f}, {1.8f, 24.45f}}, S, redRoof, T);
    addLathe(L.roof, {{1.8f, 24.45f}, {1.8f, 24.6f}}, S, redRoof, T);
    addLathe(L.roof, {{1.8f, 24.6f}, {0.18f, 26.45f}}, S, redRoof, T);
    addEllipsoid(L.roof, glm::vec3(0.28f), 16, 10, dark, T * glm::translate(glm::mat4(1.0f), glm::vec3(0, 26.55f, 0)));
    addCylinder(L.roof, 0.02f, 0.015f, 1.2f, 6, dark, T * glm::translate(glm::mat4(1.0f), glm::vec3(0, 26.7f, 0)), false);

    L.lampY = L.baseY + 23.1f;
    return L;
}

// ---------------------------------------------------------------------------
// Boat + person. Local space: +X = bow, +Y up, waterline at y = 0.
// ---------------------------------------------------------------------------
struct BoatMeshes {
    MeshData body;    // material 6: hull + deck (planks)
    MeshData cabin;   // material 7: painted parts
    MeshData glass;   // material 2: windows
    MeshData person;  // material 7
    MeshData lights;  // material 8: navigation lights (emissive at night)
    float deckY = 0.85f;
};

inline BoatMeshes makeBoat() {
    BoatMeshes B;
    const float L = 7.5f, Bh = 1.3f, draft = 0.55f, deckY = B.deckY;
    const glm::vec3 hullCol(0.07f, 0.20f, 0.36f), deckCol(0.48f, 0.34f, 0.20f);

    auto halfBeam = [=](float t) {
        if (t < 0) return Bh * (0.74f + 0.26f * (1.0f - t * t));
        return Bh * std::pow(std::max(1.0f - std::pow(t, 2.2f), 0.0f), 0.6f);
    };
    auto keelY = [=](float t) { float tp = std::max(t, 0.0f); return -draft * (1.0f - 0.55f * tp * tp); };
    auto gunY = [=](float t) { return deckY + 0.35f * t * t; };

    auto hull = [=](float u, float v) {
        float t = 2 * u - 1, s = 2 * v - 1;
        float b = halfBeam(t), k = keelY(t), g = gunY(t);
        float z = b * std::sin(s * kPi * 0.5f);
        float y = k + (g - k) * (1.0f - std::cos(std::fabs(s) * kPi * 0.5f));
        return glm::vec3(t * L * 0.5f, y, z);
    };
    addParamSurface(B.body, 48, 24, hull, hullCol, false, glm::mat4(1.0f),
                    [=](float u, float v) { glm::vec3 p = hull(u, v); return glm::vec2(p.x, p.y); });

    auto deck = [=](float u, float v) {
        float t = 2 * u - 1;
        float b = halfBeam(t);
        return glm::vec3(t * L * 0.5f, gunY(t) + 0.004f, b * (2 * v - 1));
    };
    addParamSurface(B.body, 48, 8, deck, deckCol, true, glm::mat4(1.0f),
                    [=](float u, float v) { glm::vec3 p = deck(u, v); return glm::vec2(p.x, p.z); });

    // transom (flat stern), triangle fan, normal -X
    {
        std::vector<glm::vec3> loop;
        const int N = 16;
        for (int j = 0; j <= N; ++j) loop.push_back(hull(0.0f, (float)j / N));
        for (int j = N; j >= 0; --j) { glm::vec3 d = deck(0.0f, (float)j / N); loop.push_back(d); }
        glm::vec3 c(loop[0].x, 0.5f * (-draft + gunY(-1.0f)), 0.0f);
        uint32_t base = (uint32_t)B.body.v.size();
        auto push = [&](const glm::vec3& p) {
            Vertex v; v.pos = p; v.nrm = {-1, 0, 0}; v.uv = {p.z, p.y}; v.col = hullCol;
            B.body.v.push_back(v);
        };
        push(c);
        for (auto& p : loop) push(p);
        for (size_t k = 0; k + 1 < loop.size(); ++k)
            B.body.i.insert(B.body.i.end(), {base, base + 1 + (uint32_t)k, base + 2 + (uint32_t)k});
    }

    // wheelhouse
    const glm::vec3 cream(0.86f, 0.84f, 0.78f), roofC(0.92f, 0.92f, 0.90f), black(0.03f);
    auto Tm = [](float x, float y, float z) { return glm::translate(glm::mat4(1.0f), glm::vec3(x, y, z)); };
    addBox(B.cabin, {0.85f, 0.9f, 0.8f}, cream, Tm(-0.7f, deckY + 0.9f + 0.03f, 0));
    addBox(B.cabin, {1.0f, 0.05f, 0.95f}, roofC, Tm(-0.7f, deckY + 1.86f, 0));
    addCylinder(B.cabin, 0.05f, 0.04f, 2.0f, 8, black, Tm(-1.2f, deckY + 1.9f, 0.0f));
    addBox(B.cabin, {0.12f, 0.45f, 0.12f}, black, Tm(-1.25f, deckY + 2.0f, -0.45f));  // exhaust stack
    // windows
    addBox(B.glass, {0.012f, 0.28f, 0.55f}, glm::vec3(0.9f), Tm(0.153f, deckY + 1.15f, 0));
    for (float zs : {-1.0f, 1.0f})
        addBox(B.glass, {0.45f, 0.26f, 0.012f}, glm::vec3(0.9f), Tm(-0.7f, deckY + 1.15f, zs * 0.801f));

    // navigation lights: red = port (-Z), green = starboard (+Z), white masthead
    addEllipsoid(B.lights, glm::vec3(0.07f), 10, 8, glm::vec3(1.0f, 0.05f, 0.03f), Tm(0.2f, deckY + 1.97f, -0.88f));
    addEllipsoid(B.lights, glm::vec3(0.07f), 10, 8, glm::vec3(0.05f, 1.0f, 0.15f), Tm(0.2f, deckY + 1.97f, 0.88f));
    addEllipsoid(B.lights, glm::vec3(0.07f), 10, 8, glm::vec3(1.0f, 0.95f, 0.85f), Tm(-1.2f, deckY + 3.95f, 0.0f));

    // person (standing near the bow, waving)
    const glm::vec3 jacket(0.95f, 0.70f, 0.06f), trousers(0.08f, 0.10f, 0.18f),
        skin(0.55f, 0.37f, 0.26f), boots(0.03f), hat(0.05f, 0.07f, 0.15f);
    glm::mat4 P = Tm(1.55f, deckY + 0.07f, 0.2f);
    for (float zs : {-1.0f, 1.0f}) {
        addCylinder(B.person, 0.075f, 0.09f, 0.85f, 10, trousers, P * Tm(0, 0.12f, zs * 0.1f));
        addCylinder(B.person, 0.09f, 0.085f, 0.2f, 10, boots, P * Tm(0, 0.0f, zs * 0.1f));
    }
    addEllipsoid(B.person, {0.14f, 0.32f, 0.21f}, 20, 12, jacket, P * Tm(0, 1.13f, 0));
    addCylinderBetween(B.person, {0, 1.38f, -0.22f}, {0.06f, 0.9f, -0.27f}, 0.05f, 0.04f, 8, jacket, P);
    addCylinderBetween(B.person, {0, 1.38f, 0.22f}, {0.08f, 1.82f, 0.34f}, 0.05f, 0.04f, 8, jacket, P);
    addEllipsoid(B.person, glm::vec3(0.055f), 10, 8, skin, P * Tm(0.08f, 1.86f, 0.35f));
    addEllipsoid(B.person, glm::vec3(0.05f), 10, 8, skin, P * Tm(0.06f, 0.86f, -0.27f));
    addEllipsoid(B.person, {0.10f, 0.12f, 0.10f}, 16, 12, skin, P * Tm(0, 1.62f, 0));
    addEllipsoid(B.person, {0.115f, 0.07f, 0.115f}, 16, 8, hat, P * Tm(0, 1.70f, 0));
    return B;
}

// ---------------------------------------------------------------------------
// Jetty (wooden pier) on the shore at polar angle `angle` (radians, atan2(z,x)).
// The boat docks alongside its seaward end.
// ---------------------------------------------------------------------------
struct JettyMeshes {
    MeshData deck;    // material 6
    MeshData posts;   // material 7
    glm::vec2 dir{1, 0};   // unit vector pointing out to sea (x,z)
    float rShore = 0, rEnd = 0;
};

inline JettyMeshes makeJetty(float angle) {
    JettyMeshes J;
    J.dir = glm::vec2(std::cos(angle), std::sin(angle));
    auto h = [&](float r) { return islandHeight(r * J.dir.x, r * J.dir.y); };
    float rShore = 10.0f;
    while (h(rShore) > 0.3f && rShore < 60.0f) rShore += 0.05f;
    float deckY = 0.95f;
    float rStart = rShore;
    while (h(rStart) < deckY && rStart > 2.0f) rStart -= 0.05f;
    float rEnd = rShore + 10.0f;
    J.rShore = rShore; J.rEnd = rEnd;

    float th = std::atan2(-J.dir.y, J.dir.x);
    float rMid = 0.5f * (rStart + rEnd), len = rEnd - rStart;
    glm::mat4 D = glm::translate(glm::mat4(1.0f), glm::vec3(rMid * J.dir.x, deckY - 0.06f, rMid * J.dir.y)) *
                  glm::rotate(glm::mat4(1.0f), th, glm::vec3(0, 1, 0));
    addBox(J.deck, glm::vec3(len * 0.5f, 0.06f, 1.05f), glm::vec3(0.40f, 0.28f, 0.17f), D);
    // under-beams
    for (float s : {-0.8f, 0.8f}) {
        glm::mat4 Bm = D * glm::translate(glm::mat4(1.0f), glm::vec3(0, -0.2f, s));
        addBox(J.deck, glm::vec3(len * 0.5f, 0.1f, 0.08f), glm::vec3(0.22f, 0.15f, 0.09f), Bm);
    }
    // posts
    for (float r = rShore + 0.4f; r <= rEnd - 0.1f; r += 2.4f) {
        for (float s : {-0.95f, 0.95f}) {
            glm::vec2 p = J.dir * r + glm::vec2(-J.dir.y, J.dir.x) * s;
            addCylinder(J.posts, 0.12f, 0.12f, deckY + 0.35f + 4.0f, 8, glm::vec3(0.20f, 0.14f, 0.09f),
                        glm::translate(glm::mat4(1.0f), glm::vec3(p.x, -4.0f, p.y)), false);
        }
    }
    return J;
}
