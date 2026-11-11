#pragma once
#include <cstdint>
#include <vector>

// GPU-ready sparse voxel octree.
//  nodes  : 2 x uint32 per node  -> [firstChild, validMask | leafMask << 8]
//  colors : RGBA8 per node (interior nodes = average of their children)
//  flags  : per node, CPU only (not sent to the GPU): VOXEL_GRIP, ...
// The root is node 0 and spans [1,2]^3 in ray-space.
struct Octree {
    std::vector<uint32_t> nodes;
    std::vector<uint32_t> colors;
    std::vector<uint8_t>  flags;
    int depth = 0;
    size_t nodeCount() const { return nodes.size() / 2; }
};

enum VoxelFlags : uint8_t {
    VOXEL_GRIP = 1,   // the player sticks to it when landing on it with their bottom
    VOXEL_BUILT = 2,  // placed with the build tool (part of a movable object, not the scene)
    VOXEL_SELECTED = 4, // editor selection (move tool): shown tinted; the GPU colour's alpha is 0xFE
    VOXEL_INSTANCE = 8, // a cage voxel: stands for a dream cage's content (its colour field is the cage id)
};

struct Scene;

// Voxelises a scene (see scene.hpp, scenes/) down to `depth` levels
// (depth 7 = 128^3 voxels).
Octree buildOctree(const Scene& scene, int depth);
