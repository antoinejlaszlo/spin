#pragma once
// The live, editable world: the editable voxel tree plus its GPU/physics
// serialisation. All changes go through Edit commands, applied by World::apply,
// and are recorded, so undo/redo, saving a build, or replaying edits over a
// network can be added on top without touching the tools that create them.
#include <cstdint>
#include <vector>
#include "octree.hpp"
#include "voxel_store.hpp"

struct Edit {
    enum Op : uint8_t { PLACE, REMOVE };
    Op op = PLACE;
    CellBox box;                    // cells affected (one level, inclusive)
    Voxel voxel;                    // PLACE: what to write
    float colorNoise = 0.0f;        // PLACE: +- brightness variation per cell (0 = exact colour)
    // Optional sphere that PLACE must keep clear (e.g. the player): cells that
    // intersect it are skipped. Octree space.
    bool hasKeepOut = false;
    float keepOut[4] = {0, 0, 0, 0};   // centre xyz, radius
    // Locked regions (see World::lock) are left untouched unless this is set
    // (used by the systems that own them, e.g. portals).
    bool bypassLocks = false;
};

// True if the two cell boxes (possibly at different levels) overlap in space.
bool cellBoxesOverlap(const CellBox& a, const CellBox& b);

class World {
public:
    explicit World(const Octree& initial);

    const Octree& octree() const { return oct; }   // stable object: renderer/physics keep a reference
    // Applies an edit to the editable tree. Returns the number of cells changed.
    int64_t apply(const Edit& e);
    // Re-serialises the tree into octree() if anything changed. Returns true if
    // it did (the renderer must then upload the new octree).
    bool commit();
    uint64_t version() const { return ver; }        // bumps on every commit that changed something
    // Where instance (cage) voxels find their content: octree() points them
    // at it. Content living elsewhere (the dream) can change without this
    // world's edits: markDirty() then makes the next commit() pick it up.
    void setResolver(const InstanceResolver* r) { resolver = r; dirty = true; }
    void markDirty() { dirty = true; }
    // The tree without resolving instances (they stay leaves holding the cage
    // id): what world files store.
    void rawOctree(Octree& out) const { store.toOctree(out, nullptr); }
    const VoxelStore& voxels() const { return store; }
    const std::vector<Edit>& history() const { return log; }

    // Locked regions: normal edits skip their cells (e.g. portal walls cannot
    // be broken with the block tool). Returns an id for unlock().
    int lock(const CellBox& box);
    void unlock(int id);
    // Overlaps any locked region (other than ignoreId, e.g. the one being moved).
    bool isLocked(const CellBox& box, int ignoreId = 0) const;

    static constexpr int64_t kMaxCellsPerEdit = 1 << 18;   // safety bound for one edit

    // Solid leaves overlapping the open box (lo, hi), scene units [0,1].
    void leavesIn(const double lo[3], const double hi[3], std::vector<VoxelStore::Leaf>& out) const {
        store.leavesIn(lo, hi, out);
    }
    // Editor-only flag changes (e.g. VOXEL_SELECTED) on exactly these leaves:
    // not edits, not recorded in history. commit() shows them.
    void setFlags(const std::vector<VoxelStore::Leaf>& leaves, uint8_t set, uint8_t clear);

private:
    VoxelStore store;
    Octree oct;
    const InstanceResolver* resolver = nullptr;
    bool dirty = false;
    uint64_t ver = 0;
    std::vector<Edit> log;
    std::vector<std::pair<int, CellBox>> locks;
    int nextLock = 1;
};
