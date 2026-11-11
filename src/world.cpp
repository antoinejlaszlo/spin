#include "world.hpp"
#include <algorithm>
#include <cmath>

World::World(const Octree& initial) : oct(initial) {
    store.fromOctree(initial);
}

namespace {

uint32_t noisyColor(uint32_t c, float amp, int32_t x, int32_t y, int32_t z, int level) {
    if (amp <= 0.0f) return c;
    uint32_t h = (uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u ^ (uint32_t)z * 83492791u ^ (uint32_t)level * 2654435761u;
    float n = 1.0f + (((h >> 8) & 255u) / 255.0f * 2.0f - 1.0f) * amp;
    auto ch = [&](int s) {
        float v = ((c >> s) & 255u) * n;
        return (uint32_t)std::clamp((int)std::lround(v), 0, 255) << s;
    };
    return ch(0) | ch(8) | ch(16) | (c & 0xFF000000u);
}

// Does the cell (octree space cube) intersect the sphere?
bool cellTouchesSphere(int32_t x, int32_t y, int32_t z, int level, const float s[4]) {
    float size = 1.0f / (float)(1 << level);
    float lo[3] = {1.0f + x * size, 1.0f + y * size, 1.0f + z * size};
    float d2 = 0;
    for (int a = 0; a < 3; ++a) {
        float c = std::clamp(s[a], lo[a], lo[a] + size);
        d2 += (s[a] - c) * (s[a] - c);
    }
    return d2 < s[3] * s[3];
}

} // namespace

bool cellBoxesOverlap(const CellBox& a, const CellBox& b) {
    const int m = std::max(a.level, b.level);
    const int sa = m - a.level, sb = m - b.level;
    for (int i = 0; i < 3; ++i) {
        int64_t alo = int64_t(a.lo[i]) << sa, ahi = ((int64_t(a.hi[i]) + 1) << sa) - 1;
        int64_t blo = int64_t(b.lo[i]) << sb, bhi = ((int64_t(b.hi[i]) + 1) << sb) - 1;
        if (ahi < blo || bhi < alo) return false;
    }
    return true;
}

int World::lock(const CellBox& box) { locks.push_back({nextLock, box}); return nextLock++; }
void World::unlock(int id) {
    locks.erase(std::remove_if(locks.begin(), locks.end(), [id](const auto& l) { return l.first == id; }), locks.end());
}
bool World::isLocked(const CellBox& box, int ignoreId) const {
    for (const auto& l : locks) if (l.first != ignoreId && cellBoxesOverlap(box, l.second)) return true;
    return false;
}

int64_t World::apply(const Edit& e) {
    CellBox b = e.box;
    if (b.level < 0 || b.level > VoxelStore::kMaxLevel) return 0;
    const int32_t n = 1 << b.level;
    for (int a = 0; a < 3; ++a) {               // clip to the world
        if (b.lo[a] > b.hi[a]) std::swap(b.lo[a], b.hi[a]);
        b.lo[a] = std::max(b.lo[a], 0);
        b.hi[a] = std::min(b.hi[a], n - 1);
        if (b.lo[a] > b.hi[a]) return 0;
    }
    if (b.count() > kMaxCellsPerEdit) return 0;

    // Locked regions this edit must leave alone.
    std::vector<CellBox> blocked;
    if (!e.bypassLocks)
        for (const auto& l : locks) if (cellBoxesOverlap(b, l.second)) blocked.push_back(l.second);
    auto lockedCell = [blocked, level = b.level](int32_t x, int32_t y, int32_t z) {
        CellBox c; c.level = level;
        c.lo[0] = c.hi[0] = x; c.lo[1] = c.hi[1] = y; c.lo[2] = c.hi[2] = z;
        for (const CellBox& l : blocked) if (cellBoxesOverlap(c, l)) return true;
        return false;
    };

    VoxelStore::Fill f;
    if (e.op == Edit::REMOVE) {
        f.uniform = blocked.empty();
        f.cellFn = [lockedCell](int32_t x, int32_t y, int32_t z, Voxel&, bool& empty) {
            if (lockedCell(x, y, z)) return false;
            empty = true;
            return true;
        };
    } else {
        const Edit ed = e;
        f.uniform = ed.colorNoise <= 0.0f && !ed.hasKeepOut && blocked.empty();
        f.cellFn = [ed, lockedCell, level = b.level](int32_t x, int32_t y, int32_t z, Voxel& v, bool&) {
            if (lockedCell(x, y, z)) return false;
            if (ed.hasKeepOut && cellTouchesSphere(x, y, z, level, ed.keepOut)) return false;
            v = ed.voxel;
            v.color = noisyColor(ed.voxel.color, ed.colorNoise, x, y, z, level);
            return true;
        };
    }
    int64_t changed = store.fill(b, f);
    if (changed) {
        dirty = true;
        Edit logged = e;
        logged.box = b;
        log.push_back(logged);
    }
    return changed;
}

bool World::commit() {
    if (!dirty) return false;
    store.toOctree(oct, resolver);
    dirty = false;
    ++ver;
    return true;
}

void World::setFlags(const std::vector<VoxelStore::Leaf>& leaves, uint8_t set, uint8_t clear) {
    for (const auto& l : leaves) {
        Voxel v = l.v;
        v.flags = (uint8_t)((v.flags & ~clear) | set);
        VoxelStore::Fill f;
        f.uniform = true;
        f.cellFn = [&](int32_t, int32_t, int32_t, Voxel& out, bool& empty) { out = v; empty = false; return true; };
        store.fill(l.box(), f);
    }
    if (!leaves.empty()) dirty = true;
}
