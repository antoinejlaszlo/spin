#include "voxel_store.hpp"
#include <algorithm>
#include <deque>
#include <unordered_map>
#include <unordered_set>

uint32_t VoxelStore::alloc() {
    if (!freeList.empty()) {
        uint32_t n = freeList.back();
        freeList.pop_back();
        pool[n] = Node{};
        return n;
    }
    pool.push_back(Node{});
    return (uint32_t)pool.size() - 1;
}

void VoxelStore::freeTree(uint32_t ref) {
    if (!ref) return;
    if (!pool[ref].leaf)
        for (int c = 0; c < 8; ++c) freeTree(pool[ref].child[c]);
    pool[ref] = Node{};
    freeList.push_back(ref);
}

// ---------------------------------------------------------------------------
// GPU format <-> editable tree
// ---------------------------------------------------------------------------
uint32_t VoxelStore::decode(const Octree& o, uint32_t gi) {
    uint32_t n = alloc();
    uint32_t first = o.nodes[2 * gi], masks = o.nodes[2 * gi + 1];
    uint32_t k = first;
    const bool hasFlags = o.flags.size() == o.colors.size();
    for (int s = 0; s < 8; ++s) {
        if (!(masks & (1u << s))) continue;
        uint32_t child = k++;
        uint32_t ref;
        if (masks & (1u << (s + 8))) ref = newLeaf({o.colors[child], hasFlags ? o.flags[child] : (uint8_t)0});
        else ref = decode(o, child);
        pool[n].child[s] = ref;          // index again: the pool may have grown
    }
    return n;
}

void VoxelStore::fromOctree(const Octree& o) {
    pool.assign(1, Node{});
    freeList.clear();
    root = 0;
    if (o.nodes.size() < 2) return;
    root = decode(o, 0);
    bool any = false;
    for (int c = 0; c < 8; ++c) any |= pool[root].child[c] != 0;
    if (!any) { freeTree(root); root = 0; }
}

// Writes stores into one GPU buffer. Instance voxels become copies of their
// content's root descriptor: the content is written once (per buffer) and its
// children are shared by every instance, whatever store it comes from.
struct OctreeWriter {
    Octree& out;
    const InstanceResolver* res;
    struct Avg { std::vector<uint32_t> col; std::vector<char> done; };
    std::unordered_map<const VoxelStore*, Avg> avgs;
    std::unordered_map<uint32_t, std::pair<uint32_t, uint32_t>> cages;   // id -> (descriptor index, colour)
    std::unordered_set<uint32_t> busy;                                     // cages being written (cycle guard)
    uint32_t emptyDesc = 0;                                                // shared empty node (0: none yet)

    uint32_t push(uint32_t colour, uint8_t flags) {
        out.nodes.push_back(0); out.nodes.push_back(0);
        out.colors.push_back(colour); out.flags.push_back(flags);
        return (uint32_t)out.colors.size() - 1;
    }

    // Average colour (what the renderer shows at coarse LOD); alpha 0xFE
    // marks the editor selection (VOXEL_SELECTED), shown tinted.
    uint32_t avg(const VoxelStore& s, uint32_t ref) {
        Avg& a = avgs[&s];
        if (a.col.size() < s.pool.size()) { a.col.resize(s.pool.size(), 0); a.done.resize(s.pool.size(), 0); }
        if (a.done[ref]) return a.col[ref];
        const VoxelStore::Node& nd = s.pool[ref];
        uint32_t r;
        if (nd.leaf) {
            const bool sel = nd.v.flags & VOXEL_SELECTED;
            if (isInstance(nd.v)) r = res ? (cage(nd.v.color).second & 0x00FFFFFFu) | ((sel ? 0xFEu : 0xFFu) << 24)
                                          : nd.v.color;                // raw: the cage id
            else r = sel ? (nd.v.color & 0x00FFFFFFu) | (0xFEu << 24) : (nd.v.color | 0xFF000000u);
        } else {
            float acc[3] = {0, 0, 0}; int cnt = 0;
            bool sel = false;
            for (int c = 0; c < 8; ++c) {
                if (!nd.child[c]) continue;
                uint32_t col = avg(s, nd.child[c]);
                sel = sel || (col >> 24) == 0xFEu;
                acc[0] += col & 255; acc[1] += (col >> 8) & 255; acc[2] += (col >> 16) & 255; ++cnt;
            }
            r = 0;
            if (cnt) r = (uint32_t)(acc[0] / cnt + 0.5f) | ((uint32_t)(acc[1] / cnt + 0.5f) << 8) |
                         ((uint32_t)(acc[2] / cnt + 0.5f) << 16) | ((sel ? 0xFEu : 255u) << 24);
        }
        Avg& b = avgs[&s];                   // (the map may have rehashed meanwhile)
        b.col[ref] = r; b.done[ref] = 1;
        return r;
    }

    // The descriptor node of a cage's content (written on first use) and its colour.
    std::pair<uint32_t, uint32_t> cage(uint32_t id) {
        auto it = cages.find(id);
        if (it != cages.end()) return it->second;
        InstanceSource src = res->source(id);
        if (busy.count(id) || !src.store || !src.ref) {                // a cycle, or nothing there
            if (!emptyDesc) emptyDesc = push(0xFF808080u, 0);
            return {emptyDesc, 0xFF808080u};
        }
        const VoxelStore::Node& nd = src.store->pool[src.ref];
        if (nd.leaf && isInstance(nd.v)) {                             // the content is itself an instance
            busy.insert(id);
            auto r = cage(nd.v.color);
            busy.erase(id);
            return cages[id] = r;
        }
        busy.insert(id);
        uint32_t colour = avg(*src.store, src.ref) | 0xFF000000u;      // (emits nested cages first)
        uint32_t d = push(colour, 0);
        children(*src.store, src.ref, d);
        busy.erase(id);
        return cages[id] = {d, colour};
    }

    // Writes the children of store node `ref` below GPU node `gi`, breadth-first,
    // each node's children contiguous (the GPU traversal needs that). A leaf
    // written as a parent (a solid root or cage) becomes 8 leaf children.
    void children(const VoxelStore& s, uint32_t ref, uint32_t gi) {
        std::deque<std::pair<uint32_t, uint32_t>> queue;
        queue.push_back({ref, gi});
        while (!queue.empty()) {
            auto [r, g] = queue.front();
            queue.pop_front();
            uint32_t kids[8];
            for (int c = 0; c < 8; ++c) kids[c] = s.pool[r].leaf ? r : s.pool[r].child[c];
            // Everything that may append nodes (nested content) happens before
            // this node's block of children is reserved.
            uint32_t colours[8];
            std::pair<uint32_t, uint32_t> inst[8];
            for (int c = 0; c < 8; ++c) {
                if (!kids[c]) continue;
                colours[c] = avg(s, kids[c]);
                const VoxelStore::Node& k = s.pool[kids[c]];
                if (res && k.leaf && isInstance(k.v)) inst[c] = cage(k.v.color);
            }
            uint32_t valid = 0, leafMask = 0, base = (uint32_t)out.colors.size();
            for (int c = 0; c < 8; ++c) {
                uint32_t k = kids[c];
                if (!k) continue;
                valid |= 1u << c;
                const VoxelStore::Node& kn = s.pool[k];
                uint32_t ni = push(colours[c], kn.leaf ? kn.v.flags : 0);
                if (res && kn.leaf && isInstance(kn.v)) {                 // pointer: copy the content's descriptor
                    out.nodes[2 * ni] = out.nodes[2 * inst[c].first];
                    out.nodes[2 * ni + 1] = out.nodes[2 * inst[c].first + 1];
                } else if (kn.leaf) {
                    leafMask |= 1u << c;
                } else {
                    queue.push_back({k, ni});
                }
            }
            out.nodes[2 * g] = base;
            out.nodes[2 * g + 1] = valid | (leafMask << 8);
        }
    }
};

void VoxelStore::toOctree(Octree& out, const InstanceResolver* resolver) const {
    out.nodes.clear(); out.colors.clear(); out.flags.clear();
    out.depth = maxDepth();
    out.nodes.reserve(nodeCount() * 2 + 2);
    out.colors.reserve(nodeCount() + 1);
    out.flags.reserve(nodeCount() + 1);
    if (!root) { out.nodes = {0, 0}; out.colors = {0}; out.flags = {0}; return; }
    OctreeWriter w{out, resolver, {}, {}, {}, 0};
    w.push(0, 0);                                    // the root is node 0
    const Node& r = pool[root];
    if (resolver && r.leaf && isInstance(r.v)) {    // the whole store is one instance: the root is its content
        auto c = w.cage(r.v.color);
        out.nodes[0] = out.nodes[2 * c.first]; out.nodes[1] = out.nodes[2 * c.first + 1];
        out.colors[0] = c.second;
        return;
    }
    out.colors[0] = w.avg(*this, root);
    w.children(*this, root, 0);
}

uint32_t VoxelStore::nodeAt(int level, int32_t x, int32_t y, int32_t z) const {
    uint32_t ref = root;
    for (int l = 0; l < level && ref; ++l) {
        if (pool[ref].leaf) return ref;              // a solid block covers the cell
        const int sh = level - l - 1;
        const int c = ((x >> sh) & 1) | (((y >> sh) & 1) << 1) | (((z >> sh) & 1) << 2);
        ref = pool[ref].child[c];
    }
    return ref;
}

int VoxelStore::depthRec(uint32_t ref, int level) const {
    if (!ref) return 0;
    if (pool[ref].leaf) return level;
    int d = level;
    for (int c = 0; c < 8; ++c) d = std::max(d, depthRec(pool[ref].child[c], level + 1));
    return d;
}
int VoxelStore::maxDepth() const { return root ? std::max(1, depthRec(root, 0)) : 0; }

// ---------------------------------------------------------------------------
// Editing
// ---------------------------------------------------------------------------
int64_t VoxelStore::fill(const CellBox& box, const Fill& f) {
    if (box.level < 0 || box.level > kMaxLevel || !f.cellFn) return 0;
    int64_t written = 0;
    root = fillRec(root, 0, 0, 0, 0, box, f, written);
    return written;
}

uint32_t VoxelStore::fillRec(uint32_t ref, int level, int32_t x, int32_t y, int32_t z,
                             const CellBox& box, const Fill& f, int64_t& written) {
    const int shift = box.level - level;
    const int32_t p[3] = {x, y, z};
    bool inside = true;
    for (int a = 0; a < 3; ++a) {
        int32_t lo = p[a] << shift, hi = ((p[a] + 1) << shift) - 1;   // node's cells at box.level
        if (hi < box.lo[a] || lo > box.hi[a]) return ref;            // no overlap: untouched
        if (lo < box.lo[a] || hi > box.hi[a]) inside = false;
    }

    const bool instanceHere = ref && pool[ref].leaf && isInstance(pool[ref].v);
    if (inside && (shift == 0 || f.uniform)) {
        Voxel v; bool empty = false;
        if (!f.cellFn(x << shift, y << shift, z << shift, v, empty)) return ref;
        if (empty && !ref) return 0;                                 // already empty: no change
        if (!empty && ref && pool[ref].leaf && pool[ref].v == v) return ref;   // already this voxel
        // An instance is only replaced by another instance (or the same with
        // new flags), never painted over; and a new instance is one cell, not
        // a block of several (descend to box.level below).
        if (!empty && instanceHere && !isInstance(v)) return ref;
        if (empty || !isInstance(v) || shift == 0) {
            freeTree(ref);
            written += int64_t(1) << (3 * shift);
            return empty ? 0 : newLeaf(v);
        }
    }

    // An instance only partly in the edit: removing any of it removes all of
    // it; anything else leaves it alone (it is never split).
    if (instanceHere) {
        Voxel v; bool empty = false;
        if (f.cellFn(x << shift, y << shift, z << shift, v, empty) && empty) {
            freeTree(ref);
            written += int64_t(1) << (3 * std::max(shift, 0));
            return 0;
        }
        return ref;
    }

    // Partial overlap (or per-cell values below this node): work on children.
    uint32_t n = ref;
    if (!n) {
        n = alloc();
    } else if (pool[n].leaf) {           // split a solid block into 8 copies
        Voxel v = pool[n].v;
        pool[n].leaf = false;
        for (int c = 0; c < 8; ++c) { uint32_t l = newLeaf(v); pool[n].child[c] = l; }
    }
    for (int c = 0; c < 8; ++c) {
        uint32_t child = pool[n].child[c];
        uint32_t nc = fillRec(child, level + 1, 2 * x + (c & 1), 2 * y + ((c >> 1) & 1), 2 * z + ((c >> 2) & 1),
                              box, f, written);
        pool[n].child[c] = nc;           // index again: the pool may have grown
    }

    // Normalise: drop empty nodes, merge 8 identical leaves into one.
    bool anyChild = false, allSameLeaf = true;
    for (int c = 0; c < 8; ++c) {
        uint32_t k = pool[n].child[c];
        anyChild |= k != 0;
        if (!k || !pool[k].leaf || isInstance(pool[k].v) || !(pool[k].v == pool[pool[n].child[0]].v)) allSameLeaf = false;
    }
    if (!anyChild) { pool[n] = Node{}; freeList.push_back(n); return 0; }
    if (allSameLeaf) {
        Voxel v = pool[pool[n].child[0]].v;
        for (int c = 0; c < 8; ++c) freeTree(pool[n].child[c]);
        pool[n] = Node{};
        pool[n].leaf = true;
        pool[n].v = v;
    }
    return n;
}

void VoxelStore::leavesIn(const double lo[3], const double hi[3], std::vector<Leaf>& out) const {
    if (root) leavesRec(root, 0, 0, 0, 0, lo, hi, out);
}

void VoxelStore::leavesRec(uint32_t ref, int level, int32_t x, int32_t y, int32_t z,
                           const double lo[3], const double hi[3], std::vector<Leaf>& out) const {
    const double s = 1.0 / (double)(1u << level);
    const int32_t c[3] = {x, y, z};
    for (int a = 0; a < 3; ++a)
        if (c[a] * s >= hi[a] || (c[a] + 1) * s <= lo[a]) return;
    const Node& nd = pool[ref];
    if (nd.leaf) { out.push_back({level, x, y, z, nd.v}); return; }
    for (int k = 0; k < 8; ++k)
        if (nd.child[k])
            leavesRec(nd.child[k], level + 1, 2 * x + (k & 1), 2 * y + ((k >> 1) & 1), 2 * z + ((k >> 2) & 1), lo, hi, out);
}
