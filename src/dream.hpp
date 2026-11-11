#pragma once
// The dream: a separate world, next to the levels, where models are built.
// An indestructible flat ground with a grid of cages. A cage is a box about
// the player's size, outlined by thin bars; you build inside it (and only
// there), and its content is a model you can take back to the levels and
// place as many times as you like (ObjectTool).
#include <memory>
#include <vector>
#include "level.hpp"
#include "voxel_store.hpp"

class Dream : public InstanceResolver {
public:
    // Cages are cubes of 1/16 of the world (8 default blocks), every 1/8.
    static constexpr int kCageLevel = 4;          // octree level whose cell is one cage interior

    explicit Dream(const Octree& o) : level("the dream", o) {}
    // A new dream for worlds whose default block is at `baseLevel`.
    static std::shared_ptr<Dream> create(int baseLevel);

    Level level;                     // its world (no portals, no start)
    std::vector<CellBox> cages;      // cage interiors (at the base level)

    // The cage whose box (bars included) the ray enters first within maxT; -1 none.
    int pickCage(const float eye[3], const float dir[3], float maxT) const;
    // Where you appear when you come to the dream: on the ground, between cages.
    Vec3 spawn(float radius, float& yaw) const;
    int baseLevel() const { return cages.empty() ? 7 : cages[0].level; }
    // Octree-space bounds of a cage, bars included.
    void cageBounds(int i, float lo[3], float hi[3]) const;

    // Instances: cage voxels refer to a cage by its index. Their content is
    // the dream's subtree at the cage (cage interiors are level-kCageLevel cells).
    InstanceSource source(uint32_t id) const override;
    // The cage a box (any level) lies entirely in, or -1.
    int cageOf(const CellBox& b) const;
    // Does cage a hold (directly or through nested instances) an instance of cage b?
    bool contains(int a, int b) const;
    // The cages instanced in cage a (directly).
    std::vector<int> instancesIn(int a) const;
};

// Points every level (and the dream itself) at the dream for instance
// content, and re-serialises them. Call after creating or loading a world.
void linkDream(LevelSet& levels);
