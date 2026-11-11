// "island" — the first world: rolling voxel terrain (grass / rock / snow by
// altitude), a striped red sphere floating above it, and a tilted golden ring
// around the sphere. Saved exactly as it was rendered (geometry, colours and
// per-voxel colour noise). Select it with:  octree_render --scene island
#include "scene.hpp"

using namespace scn;

namespace {

enum Material { M_TERRAIN, M_SPHERE, M_TORUS };

float terrainHeight(float x, float z) {
    return 0.22f
         + 0.050f * std::sin(7.0f * x) * std::cos(6.0f * z)
         + 0.025f * std::sin(19.0f * x + 4.0f * z)
         + 0.012f * std::cos(41.0f * z - 13.0f * x);
}

float sdfTerrain(V3 p) {
    // Height fields are not true distances; 0.45 bounds the slope (~2.1).
    return (p.y - terrainHeight(p.x, p.z)) * 0.45f;
}
float sdfSphere(V3 p) { return len(p - V3{0.5f, 0.50f, 0.5f}) - 0.17f; }
float sdfTorus(V3 p) {
    V3 q = p - V3{0.5f, 0.50f, 0.5f};
    // Tilted ring around the sphere.
    float c = 0.92f, s = 0.39f;
    float y = q.y * c - q.z * s, z = q.y * s + q.z * c;
    float r = std::sqrt(q.x * q.x + z * z) - 0.29f;
    return std::sqrt(r * r + y * y) - 0.035f;
}

float sceneSdf(V3 p, Material* m) {
    float a = sdfTerrain(p), b = sdfSphere(p), c = sdfTorus(p);
    float d = a; Material mm = M_TERRAIN;
    if (b < d) { d = b; mm = M_SPHERE; }
    if (c < d) { d = c; mm = M_TORUS; }
    if (m) *m = mm;
    return d;
}

// "Textures": colour per material, with a cheap per-voxel hash for noise.
uint32_t materialColor(V3 p) {
    Material m;
    sceneSdf(p, &m);
    uint32_t h = (uint32_t)(p.x * 4096.0f) * 73856093u ^ (uint32_t)(p.y * 4096.0f) * 19349663u ^
                 (uint32_t)(p.z * 4096.0f) * 83492791u;
    float n = ((h >> 8) & 255u) / 255.0f * 0.12f - 0.06f;
    switch (m) {
    case M_SPHERE: {   // red with horizontal stripes
        float band = std::sin((p.y - 0.5f) * 90.0f) > 0.0f ? 1.0f : 0.8f;
        return rgba((0.80f + n) * band, (0.22f + n) * band, (0.18f + n) * band);
    }
    case M_TORUS:      // gold
        return rgba(0.95f + n, 0.72f + n, 0.25f + n);
    default: {         // terrain by altitude
        float h01 = std::clamp((p.y - 0.13f) / 0.20f, 0.0f, 1.0f);
        if (h01 > 0.82f) return rgba(0.92f + n, 0.93f + n, 0.96f + n);          // snow
        if (h01 > 0.62f) return rgba(0.50f + n, 0.46f + n, 0.42f + n);          // rock
        return rgba(0.30f + n, 0.55f + n * 2.0f - 0.1f * h01, 0.22f + n);         // grass
    }
    }
}

float sdf(V3 p, float) { return sceneSdf(p, nullptr); }
uint32_t color(V3 p, float) { return materialColor(p); }

} // namespace

extern const Scene kIslandScene = {
    "island",
    "rolling terrain (grass/rock/snow), striped red sphere, tilted gold ring",
    sdf, color, nullptr};
