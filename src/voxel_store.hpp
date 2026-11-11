#pragma once
// Editable sparse voxel octree (the CPU-side truth of the world).
//
// The GPU format (Octree: compact, breadth-first, children packed) is great to
// trace but awkward to edit, so edits happen here, in a pointer-style tree,
// and toOctree() re-serialises it for the renderer and the physics.
//
// Space: the root cube is [0,1]^3 in scene units (= [1,2]^3 in octree space).
// A "cell" at level L is one of the 2^L x 2^L x 2^L cubes of edge 2^-L.
#include <cstdint>
#include <functional>
#include <vector>
#include "octree.hpp"

struct Voxel {
    uint32_t color = 0xFFFFFFFFu;   // RGBA8
    uint8_t flags = 0;              // VoxelFlags (VOXEL_GRIP, ...)
    bool operator==(const Voxel& o) const { return color == o.color && flags == o.flags; }
};

// Instance voxels ("cage voxels", flag VOXEL_INSTANCE): one leaf that stands
// for the whole content of a dream cage, scaled to the leaf's cell. Its colour
// field holds the cage id. The store keeps it as one unit: it is never split,
// merged with identical neighbours or painted over; an edit that removes any
// of its cells removes all of it. When serialised with a resolver, it becomes
// a pointer: its node shares the cage content's nodes (a DAG), so changes to
// the cage show in every instance.
inline bool isInstance(const Voxel& v) { return (v.flags & VOXEL_INSTANCE) != 0; }

class VoxelStore;
// Where an instance's content lives: a node of some store (0 = empty).
struct InstanceSource { const VoxelStore* store = nullptr; uint32_t ref = 0; };
class InstanceResolver {
public:
    virtual ~InstanceResolver() = default;
    virtual InstanceSource source(uint32_t id) const = 0;
};

struct CellBox {                    // inclusive range of cells at one level
    int level = 0;
    int32_t lo[3] = {0, 0, 0}, hi[3] = {0, 0, 0};
    int64_t count() const {
        return int64_t(hi[0] - lo[0] + 1) * (hi[1] - lo[1] + 1) * (hi[2] - lo[2] + 1);
    }
};

class VoxelStore {
public:
    static constexpr int kMaxLevel = 16;

    void fromOctree(const Octree& o);
    // GPU format. With a resolver, instance voxels point at their content
    // (written once, shared by all instances, nested ones too; a cycle is cut
    // and shows as empty). Without one, they stay plain leaves whose colour is
    // the cage id: the raw format files use (fromOctree reads it back).
    void toOctree(Octree& out, const InstanceResolver* resolver = nullptr) const;
    // The node standing for the cell (level, x, y, z): the subtree there, a
    // leaf covering it, or 0 if it is empty.
    uint32_t nodeAt(int level, int32_t x, int32_t y, int32_t z) const;

    // For every cell of `box`: cellFn(x, y, z, voxel) decides. Return true to
    // write `voxel` (solid), set `empty = true` to clear the cell instead, or
    // return false to leave the cell as it is. `uniform` (optional) says every
    // cell gets the same result, which lets whole subtrees be replaced at once.
    struct Fill {
        std::function<bool(int32_t x, int32_t y, int32_t z, Voxel& v, bool& empty)> cellFn;
        bool uniform = false;
    };
    // Returns the number of cells written.
    int64_t fill(const CellBox& box, const Fill& f);

    // A solid leaf: one cell at `level` (it may stand for many finer cells).
    struct Leaf {
        int level = 0;
        int32_t x = 0, y = 0, z = 0;
        Voxel v;
        CellBox box() const { CellBox b; b.level = level; b.lo[0] = b.hi[0] = x; b.lo[1] = b.hi[1] = y; b.lo[2] = b.hi[2] = z; return b; }
    };
    // All solid leaves overlapping the open box (lo, hi), scene units [0,1].
    void leavesIn(const double lo[3], const double hi[3], std::vector<Leaf>& out) const;

    size_t nodeCount() const { return pool.size() - freeList.size() - 1; }
    int maxDepth() const;

private:
    friend struct OctreeWriter;
    struct Node {
        uint32_t child[8] = {0, 0, 0, 0, 0, 0, 0, 0};   // 0 = empty, else pool index
        bool leaf = false;
        Voxel v;
    };
    std::vector<Node> pool{1};       // pool[0] is unused: index 0 means "empty"
    std::vector<uint32_t> freeList;
    uint32_t root = 0;

    uint32_t alloc();
    void freeTree(uint32_t ref);
    uint32_t newLeaf(const Voxel& v) { uint32_t n = alloc(); pool[n].leaf = true; pool[n].v = v; return n; }
    uint32_t fillRec(uint32_t ref, int level, int32_t x, int32_t y, int32_t z,
                     const CellBox& box, const Fill& f, int64_t& written);
    uint32_t decode(const Octree& o, uint32_t gpuIndex);
    int depthRec(uint32_t ref, int level) const;
    void leavesRec(uint32_t ref, int level, int32_t x, int32_t y, int32_t z,
                   const double lo[3], const double hi[3], std::vector<Leaf>& out) const;
};
