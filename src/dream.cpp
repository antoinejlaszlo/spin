#include "dream.hpp"
#include "physics.hpp"
#include <algorithm>
#include <cmath>

namespace {

uint32_t rgb(int r, int g, int b) { return 0xFF000000u | (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16); }

void fillBox(VoxelStore& st, const CellBox& b, uint32_t color) {
    VoxelStore::Fill f;
    f.uniform = true;
    Voxel v; v.color = color; v.flags = 0;
    f.cellFn = [v](int32_t, int32_t, int32_t, Voxel& out, bool& empty) { out = v; empty = false; return true; };
    st.fill(b, f);
}

CellBox box(int level, int32_t x0, int32_t x1, int32_t y0, int32_t y1, int32_t z0, int32_t z1) {
    CellBox b; b.level = level;
    b.lo[0] = x0; b.hi[0] = x1; b.lo[1] = y0; b.hi[1] = y1; b.lo[2] = z0; b.hi[2] = z1;
    return b;
}

} // namespace

std::shared_ptr<Dream> Dream::create(int base) {
    base = std::max(base, kCageLevel + 1);
    const int32_t n = 1 << base;                         // cells per axis
    const int32_t side = 1 << (base - kCageLevel);       // cage interior, in cells
    const int32_t pitch = 2 * side;
    const int32_t ground = n / 4 - 1;                    // ground layer: its top face is at n / 4, so cages
                                                         // (interiors from n / 4 up) line up with level kCageLevel
    VoxelStore st;
    // Ground: soft tiles, one per cage pitch, so you can tell where you are.
    for (int32_t tz = 0; tz < n / pitch; ++tz)
        for (int32_t tx = 0; tx < n / pitch; ++tx)
            fillBox(st, box(base, tx * pitch, tx * pitch + pitch - 1, ground, ground, tz * pitch, tz * pitch + pitch - 1),
                    (tx + tz) % 2 ? rgb(196, 186, 226) : rgb(182, 172, 214));
    std::vector<CellBox> cages;
    // Cages: interiors [x0, x0 + side) with x0 = side, 3 side, 5 side, ...
    for (int32_t cz = side; cz + side <= n - side; cz += pitch)
        for (int32_t cx = side; cx + side <= n - side; cx += pitch)
            cages.push_back(box(base, cx, cx + side - 1, ground + 1, ground + side, cz, cz + side - 1));
    // Bars: 1/4 of a default block thick, just outside each interior (the
    // bottom ones lie in the ground).
    const int bl = base + 2;
    const uint32_t bar = rgb(246, 240, 255);
    for (const CellBox& c : cages) {
        const int32_t X0 = c.lo[0] * 4, X1 = (c.hi[0] + 1) * 4, Y0 = c.lo[1] * 4, Y1 = (c.hi[1] + 1) * 4;
        const int32_t Z0 = c.lo[2] * 4, Z1 = (c.hi[2] + 1) * 4;
        for (int32_t x : {X0 - 1, X1})
            for (int32_t z : {Z0 - 1, Z1}) fillBox(st, box(bl, x, x, Y0 - 1, Y1, z, z), bar);          // uprights
        for (int32_t y : {Y0 - 1, Y1}) {
            for (int32_t z : {Z0 - 1, Z1}) fillBox(st, box(bl, X0 - 1, X1, y, y, z, z), bar);          // along x
            for (int32_t x : {X0 - 1, X1}) fillBox(st, box(bl, x, x, y, y, Z0 - 1, Z1), bar);          // along z
        }
    }
    Octree o;
    st.toOctree(o);
    auto d = std::make_shared<Dream>(o);
    d->cages = std::move(cages);
    return d;
}

void Dream::cageBounds(int i, float lo[3], float hi[3]) const {
    const CellBox& c = cages[i];
    const float s = 1.0f / (float)(1 << c.level), bar = 0.25f * s;
    for (int a = 0; a < 3; ++a) { lo[a] = 1.0f + c.lo[a] * s - bar; hi[a] = 1.0f + (c.hi[a] + 1) * s + bar; }
}

int Dream::pickCage(const float e[3], const float d[3], float maxT) const {
    int best = -1;
    float bestT = maxT;
    for (int i = 0; i < (int)cages.size(); ++i) {
        float lo[3], hi[3];
        cageBounds(i, lo, hi);
        float t0 = 0, t1 = 1e30f;
        bool hit = true;
        for (int a = 0; a < 3 && hit; ++a) {
            if (std::fabs(d[a]) < 1e-9f) { if (e[a] < lo[a] || e[a] > hi[a]) hit = false; continue; }
            float ta = (lo[a] - e[a]) / d[a], tb = (hi[a] - e[a]) / d[a];
            if (ta > tb) std::swap(ta, tb);
            t0 = std::max(t0, ta); t1 = std::min(t1, tb);
            if (t0 > t1) hit = false;
        }
        if (hit && t0 < bestT) { bestT = t0; best = i; }
    }
    return best;
}

Vec3 Dream::spawn(float radius, float& yaw) const {
    const int base = baseLevel();
    const float s = 1.0f / (float)(1 << base);
    const int32_t side = 1 << (base - kCageLevel);
    // In the first aisle (cages are at [side, 2 side), [3 side, 4 side), ...),
    // facing the second cage of the first column, straight on (+z).
    yaw = 1.5707963f;
    return {1.0f + 1.5f * side * s, 1.0f + ((1 << base) / 4) * s + radius + 1e-4f, 1.0f + 2.5f * side * s};
}

InstanceSource Dream::source(uint32_t id) const {
    if (id >= cages.size()) return {};
    const CellBox& c = cages[id];
    const int sh = c.level - kCageLevel;
    const VoxelStore& st = level.world.voxels();
    return {&st, st.nodeAt(kCageLevel, c.lo[0] >> sh, c.lo[1] >> sh, c.lo[2] >> sh)};
}

int Dream::cageOf(const CellBox& b) const {
    for (int i = 0; i < (int)cages.size(); ++i) {
        const CellBox& c = cages[i];
        bool in = true;
        for (int a = 0; a < 3 && in; ++a) {
            // compare at the finer of the two levels
            const int L = std::max(b.level, c.level);
            const int64_t blo = (int64_t)b.lo[a] << (L - b.level), bhi = ((int64_t)b.hi[a] + 1) << (L - b.level);
            const int64_t clo = (int64_t)c.lo[a] << (L - c.level), chi = ((int64_t)c.hi[a] + 1) << (L - c.level);
            in = blo >= clo && bhi <= chi;
        }
        if (in) return i;
    }
    return -1;
}

std::vector<int> Dream::instancesIn(int a) const {
    std::vector<int> ids;
    if (a < 0 || a >= (int)cages.size()) return ids;
    const CellBox& c = cages[a];
    const double s = 1.0 / (double)(1u << c.level);
    double lo[3], hi[3];
    for (int k = 0; k < 3; ++k) { lo[k] = c.lo[k] * s + 1e-9; hi[k] = (c.hi[k] + 1) * s - 1e-9; }
    std::vector<VoxelStore::Leaf> found;
    level.world.leavesIn(lo, hi, found);
    for (const auto& l : found)
        if (isInstance(l.v) && std::find(ids.begin(), ids.end(), (int)l.v.color) == ids.end()) ids.push_back((int)l.v.color);
    return ids;
}

bool Dream::contains(int a, int b) const {
    std::vector<int> todo{a}, seen;
    while (!todo.empty()) {
        int x = todo.back(); todo.pop_back();
        if (std::find(seen.begin(), seen.end(), x) != seen.end()) continue;
        seen.push_back(x);
        for (int y : instancesIn(x)) {
            if (y == b) return true;
            todo.push_back(y);
        }
    }
    return false;
}

void linkDream(LevelSet& levels) {
    if (!levels.dream) return;
    for (int i = 0; i < levels.count(); ++i) { levels.at(i).world.setResolver(levels.dream.get()); levels.at(i).world.commit(); }
    levels.dream->level.world.setResolver(levels.dream.get());
    levels.dream->level.world.commit();
}
