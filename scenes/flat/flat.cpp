// "flat" — a flat platform exactly one voxel thick, covering the whole world
// horizontally, a quarter of the way up. Light grey/blue checkerboard
// (4x4-voxel squares) with a little per-voxel noise so motion is visible.
//
// Two blue GRIP walls stand on it to test wall-walking:
//   wall 1: the plane x = 0.60, facing -x (toward the spawn point),
//   wall 2: the plane z = 0.75, facing -z, perpendicular to wall 1 and meeting
//           it in an inner corner, so you can go ground -> wall 1 -> wall 2.
#include "scene.hpp"

using namespace scn;

namespace {

// Layer index of the platform for the current resolution.
int layer(float v) { return (int)std::floor(0.25f / v); }

float slab(V3 p, float v) {
    // A slab covering exactly one voxel layer: voxel centres in that layer are
    // at distance -v/2 (inside), the layers above and below at +v/2.
    float centre = (layer(v) + 0.5f) * v;
    return std::fabs(p.y - centre) - 0.5f * v;
}

// Exact signed distance to an axis-aligned box.
float box(V3 p, V3 lo, V3 hi) {
    V3 c = (lo + hi) * 0.5f, h = (hi - lo) * 0.5f;   // centre, half size
    float qx = std::fabs(p.x - c.x) - h.x, qy = std::fabs(p.y - c.y) - h.y, qz = std::fabs(p.z - c.z) - h.z;
    float ox = std::max(qx, 0.0f), oy = std::max(qy, 0.0f), oz = std::max(qz, 0.0f);
    return std::sqrt(ox * ox + oy * oy + oz * oz) + std::min(std::max(qx, std::max(qy, qz)), 0.0f);
}

// Walls snapped to the voxel grid (2 voxels thick), standing on the platform.
float snap(float a, float v) { return std::floor(a / v) * v; }
struct Walls { V3 lo1, hi1, lo2, hi2; };
Walls walls(float v) {
    float y0 = (layer(v) + 1) * v, y1 = snap(0.62f, v);
    float x1 = snap(0.60f, v), z2 = snap(0.75f, v), t = 2 * v;
    return {{x1, y0, snap(0.30f, v)}, {x1 + t, y1, z2 + t},     // wall 1 (x = 0.60 face)
            {snap(0.35f, v), y0, z2}, {x1 + t, y1, z2 + t}};    // wall 2 (z = 0.75 face)
}
float wallsSdf(V3 p, float v) {
    Walls w = walls(v);
    return std::min(box(p, w.lo1, w.hi1), box(p, w.lo2, w.hi2));
}

float sdf(V3 p, float v) { return std::min(slab(p, v), wallsSdf(p, v)); }

bool grip(V3 p, float v) { return wallsSdf(p, v) <= 0.0f; }

uint32_t color(V3 p, float v) {
    int ix = (int)std::floor(p.x / v), iy = (int)std::floor(p.y / v), iz = (int)std::floor(p.z / v);
    uint32_t h = (uint32_t)ix * 73856093u ^ (uint32_t)iy * 19349663u ^ (uint32_t)iz * 83492791u;
    float n = ((h >> 8) & 255u) / 255.0f * 0.06f - 0.03f;
    if (grip(p, v)) return rgba(0.16f + n, 0.36f + n, 0.92f + n);   // blue = grip
    bool dark = (((ix >> 2) + (iz >> 2)) & 1) != 0;
    return dark ? rgba(0.52f + n, 0.56f + n, 0.62f + n) : rgba(0.78f + n, 0.80f + n, 0.83f + n);
}

} // namespace

extern const Scene kFlatScene = {
    "flat",
    "flat checkerboard platform (1 voxel thick) with two blue grip walls",
    sdf, color, grip};
