#pragma once
// Scene interface: a world is a signed distance function plus a colour rule,
// both defined in [0,1]^3 (the octree maps that to [1,2]^3 for ray casting).
// Scenes live in scenes/<name>/ and are registered in scenes/scenes.cpp.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace scn {

struct V3 { float x, y, z; };
inline V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float len(V3 a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }

inline uint32_t rgba(float r, float g, float b) {
    auto c = [](float v) { return (uint32_t)std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f); };
    return c(r) | (c(g) << 8) | (c(b) << 16) | (255u << 24);
}

} // namespace scn

struct Scene {
    const char* name;
    const char* description;
    // Signed distance: negative inside solid. Must not over-estimate the true
    // distance (|grad| <= 1), since the builder skips cubes with |sdf| > radius.
    float (*sdf)(scn::V3 p, float voxelSize);
    // Colour (RGBA8) of the solid voxel whose centre is p.
    uint32_t (*color)(scn::V3 p, float voxelSize);
    // Optional: true if the solid voxel whose centre is p has grip.
    bool (*grip)(scn::V3 p, float voxelSize) = nullptr;
};

const std::vector<const Scene*>& allScenes();
const Scene* findScene(const std::string& name);   // nullptr if unknown
