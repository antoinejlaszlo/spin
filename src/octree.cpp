#include "octree.hpp"
#include "scene.hpp"
#include <cmath>
#include <algorithm>
#include <deque>

namespace {

using scn::V3;

uint32_t popc8(uint32_t v) { uint32_t c = 0; for (; v; v &= v - 1) ++c; return c; }

// ---------------------------------------------------------------------------
// Pass 1: top-down subdivision. May leave a few interior nodes whose subtree
// turned out empty (the SDF test is conservative); pass 2 prunes them.
// ---------------------------------------------------------------------------
struct Builder {
    int maxDepth;
    const Scene* scene;
    float voxel;   // edge of a finest-level voxel in [0,1] units
    std::vector<uint32_t> first;     // first child index
    std::vector<uint16_t> masks;     // valid | leaf << 8
    std::vector<uint8_t>  isLeaf;
    std::vector<uint8_t>  flags;     // per node, VOXEL_GRIP etc. (leaves only)
    std::vector<uint32_t> color;

    uint32_t alloc(uint32_t n) {
        uint32_t base = (uint32_t)first.size();
        first.resize(base + n, 0); masks.resize(base + n, 0);
        isLeaf.resize(base + n, 0); color.resize(base + n, 0); flags.resize(base + n, 0);
        return base;
    }

    void build(uint32_t idx, V3 c, float h, int depth) {
        const float hh = h * 0.5f;            // child half-size
        const float r  = hh * 1.7320508f;     // child bounding-sphere radius
        uint32_t valid = 0, leaf = 0;
        V3 cc[8];
        for (int i = 0; i < 8; ++i) {
            cc[i] = c + V3{(i & 1) ? hh : -hh, (i & 2) ? hh : -hh, (i & 4) ? hh : -hh};
            float s = scene->sdf(cc[i], voxel);
            if (s > r) continue;                                   // empty
            if (s < -r) { valid |= 1u << i; leaf |= 1u << i; continue; } // solid block
            if (depth + 1 >= maxDepth) {                           // finest level
                if (s <= 0.0f) { valid |= 1u << i; leaf |= 1u << i; }
                continue;
            }
            valid |= 1u << i;                                      // straddles surface
        }
        uint32_t base = alloc(popc8(valid));
        first[idx] = base;
        masks[idx] = (uint16_t)(valid | (leaf << 8));
        uint32_t k = base;
        for (int i = 0; i < 8; ++i) {
            if (!(valid & (1u << i))) continue;
            if (leaf & (1u << i)) {
                isLeaf[k] = 1;
                color[k] = scene->color(cc[i], voxel);
                if (scene->grip && scene->grip(cc[i], voxel)) flags[k] |= VOXEL_GRIP;
            }
            else build(k, cc[i], hh, depth + 1);
            ++k;
        }
    }
};

} // namespace

Octree buildOctree(const Scene& scene, int depth) {
    Builder b;
    b.maxDepth = depth;
    b.scene = &scene;
    b.voxel = 1.0f / (float)(1 << depth);
    b.alloc(1);
    b.build(0, V3{0.5f, 0.5f, 0.5f}, 0.5f, 0);

    // Pass 2 (bottom-up): children always have larger indices than their
    // parent, so a reverse sweep sees children first. Compute non-emptiness
    // and average colours for LOD.
    const size_t n = b.first.size();
    std::vector<uint8_t> nonEmpty(n, 0);
    for (size_t i = n; i-- > 0;) {
        if (b.isLeaf[i]) { nonEmpty[i] = 1; continue; }
        uint32_t valid = b.masks[i] & 0xFF, k = b.first[i];
        float acc[3] = {0, 0, 0}; int cnt = 0;
        for (int s = 0; s < 8; ++s) {
            if (!(valid & (1u << s))) continue;
            if (nonEmpty[k]) {
                uint32_t c = b.color[k];
                acc[0] += c & 255; acc[1] += (c >> 8) & 255; acc[2] += (c >> 16) & 255; ++cnt;
            }
            ++k;
        }
        if (cnt) {
            nonEmpty[i] = 1;
            b.color[i] = (uint32_t)(acc[0] / cnt + 0.5f) | ((uint32_t)(acc[1] / cnt + 0.5f) << 8) |
                         ((uint32_t)(acc[2] / cnt + 0.5f) << 16) | (255u << 24);
        }
    }

    // Pass 3 (breadth-first re-serialisation): drop empty subtrees and lay
    // nodes out level by level, which is friendlier to GPU caches.
    Octree out;
    out.depth = depth;
    out.nodes.reserve(n * 2);
    out.colors.reserve(n);
    out.flags.reserve(n);
    out.nodes.push_back(0); out.nodes.push_back(0);
    out.colors.push_back(b.color[0]);
    out.flags.push_back(b.flags[0]);

    std::deque<std::pair<uint32_t, uint32_t>> queue;   // (old index, new index)
    if (nonEmpty[0]) queue.push_back({0, 0});
    while (!queue.empty()) {
        auto [oi, ni] = queue.front();
        queue.pop_front();
        if (b.isLeaf[oi]) continue;
        uint32_t valid = b.masks[oi] & 0xFF, leaf = b.masks[oi] >> 8, k = b.first[oi];
        uint32_t newValid = 0, newLeaf = 0;
        uint32_t base = (uint32_t)out.colors.size();
        for (int s = 0; s < 8; ++s) {
            if (!(valid & (1u << s))) continue;
            if (nonEmpty[k]) {
                newValid |= 1u << s;
                if (leaf & (1u << s)) newLeaf |= 1u << s;
                uint32_t nk = (uint32_t)out.colors.size();
                out.nodes.push_back(0); out.nodes.push_back(0);
                out.colors.push_back(b.color[k]);
                out.flags.push_back(b.flags[k]);
                queue.push_back({k, nk});
            }
            ++k;
        }
        out.nodes[2 * ni]     = base;
        out.nodes[2 * ni + 1] = newValid | (newLeaf << 8);
    }
    return out;
}
