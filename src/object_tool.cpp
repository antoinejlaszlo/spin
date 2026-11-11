#include "object_tool.hpp"
#include "dream.hpp"
#include <algorithm>
#include <cmath>

Model extractModel(const World& world, const CellBox& interior, int cageLevel, const InstanceResolver* resolver) {
    Model m;
    m.cageLevel = cageLevel;
    const int b = interior.level;
    const double s = 1.0 / (double)(1u << b);
    double lo[3], hi[3];
    int32_t cage[3];                                   // the cage as a cell at cageLevel
    for (int a = 0; a < 3; ++a) {
        lo[a] = interior.lo[a] * s + 1e-9; hi[a] = (interior.hi[a] + 1) * s - 1e-9;
        cage[a] = interior.lo[a] >> (b - cageLevel);
    }
    std::vector<VoxelStore::Leaf> found;
    world.leavesIn(lo, hi, found);
    m.minLevel = VoxelStore::kMaxLevel; m.maxLevel = 0;
    for (VoxelStore::Leaf l : found) {
        if (l.level < cageLevel) continue;             // larger than the cage: not inside it
        int32_t c[3] = {l.x, l.y, l.z};
        bool inside = true;
        for (int a = 0; a < 3; ++a) {
            int32_t local = c[a] - (cage[a] << (l.level - cageLevel));
            if (local < 0 || local >= (1 << (l.level - cageLevel))) inside = false;
            c[a] = local;
        }
        if (!inside) continue;
        l.x = c[0]; l.y = c[1]; l.z = c[2];
        l.v.flags = (uint8_t)((l.v.flags & ~VOXEL_SELECTED) | VOXEL_BUILT);   // copies are movable objects
        m.leaves.push_back(l);
        m.minLevel = std::min(m.minLevel, l.level);
        m.maxLevel = std::max(m.maxLevel, l.level);
    }
    if (m.leaves.empty()) return m;
    for (int a = 0; a < 3; ++a) { m.lo[a] = INT32_MAX; m.hi[a] = INT32_MIN; }
    for (const auto& l : m.leaves) {
        const int sh = m.maxLevel - l.level;
        const int32_t c[3] = {l.x, l.y, l.z};
        for (int a = 0; a < 3; ++a) {
            m.lo[a] = std::min(m.lo[a], c[a] << sh);
            m.hi[a] = std::max(m.hi[a], (c[a] + 1) << sh);
        }
    }
    buildGhost(m, resolver);
    return m;
}

void buildGhost(Model& m, const InstanceResolver* resolver) {
    VoxelStore g;
    for (const auto& l : m.leaves) {
        CellBox cb; cb.level = l.level - m.cageLevel;
        cb.lo[0] = cb.hi[0] = l.x; cb.lo[1] = cb.hi[1] = l.y; cb.lo[2] = cb.hi[2] = l.z;
        VoxelStore::Fill f;
        f.uniform = true;
        Voxel v = l.v;
        f.cellFn = [v](int32_t, int32_t, int32_t, Voxel& out, bool& empty) { out = v; empty = false; return true; };
        g.fill(cb, f);
    }
    g.toOctree(m.ghost, resolver);
}

Model makeReference(const Dream& dream, int cage) {
    Model m;
    m.reference = true;
    m.cage = cage;
    m.cageLevel = Dream::kCageLevel;
    VoxelStore::Leaf l;
    l.level = Dream::kCageLevel;              // the whole cage, as one cell
    l.v.color = (uint32_t)cage;
    l.v.flags = VOXEL_INSTANCE | VOXEL_BUILT;
    m.leaves.push_back(l);
    m.minLevel = m.maxLevel = Dream::kCageLevel;
    for (int a = 0; a < 3; ++a) { m.lo[a] = 0; m.hi[a] = 1; }   // it owns its whole cube
    buildGhost(m, &dream);
    return m;
}

void ObjectTool::hold(Model m) {
    model = std::move(m);
    valid = false;
    lastK = 1 << 20;
    lastVersion = ~0ull;
}

const char* ObjectTool::fits(const World& world, const float* keepOut, const Dream* intoDream) const {
    const int P = placeLevel();
    std::vector<VoxelStore::Leaf> found;
    int cage = -2;                                                     // the dream cage it goes in
    for (const auto& l : model.leaves) {
        const int lv = l.level - k;
        const int sh = lv - P;
        const int64_t n = 1ll << lv;
        const int32_t local[3] = {l.x, l.y, l.z};
        CellBox cb; cb.level = lv;
        double lo[3], hi[3];
        const double s = 1.0 / (double)n;
        for (int a = 0; a < 3; ++a) {
            int64_t c = ((int64_t)origin[a] << sh) + local[a];
            if (c < 0 || c >= n) return "no room here";             // outside the world
            cb.lo[a] = cb.hi[a] = (int32_t)c;
            lo[a] = c * s + 1e-9; hi[a] = (c + 1) * s - 1e-9;
        }
        found.clear();
        world.leavesIn(lo, hi, found);
        if (!found.empty()) return "no room here";                 // overlaps voxels
        if (world.isLocked(cb)) return "no room here";             // doors, start box
        if (intoDream) {                                           // inside one cage
            int c = intoDream->cageOf(cb);
            if (c < 0 || (cage != -2 && c != cage)) return "it must fit inside one cage";
            cage = c;
        }
        if (keepOut) {                                             // the player
            float d2 = 0;
            for (int a = 0; a < 3; ++a) {
                float q = std::clamp(keepOut[a], 1.0f + (float)lo[a], 1.0f + (float)hi[a]);
                d2 += (keepOut[a] - q) * (keepOut[a] - q);
            }
            if (d2 < keepOut[3] * keepOut[3]) return "no room here";
        }
    }
    // A cage can't hold itself, not even through nested cage voxels.
    if (intoDream && cage >= 0)
        for (const auto& l : model.leaves)
            if (isInstance(l.v) && ((int)l.v.color == cage || intoDream->contains((int)l.v.color, cage)))
                return "a cage can't contain itself";
    return nullptr;
}

void ObjectTool::aim(const PickHit& h, const float eye[3], const float dir[3], const World& world, int scaleShift,
                     const float* keepOut, const Dream* intoDream) {
    valid = false;
    reason = "";
    if (!holding()) return;
    k = scaleShift;
    const int P = placeLevel();
    if (P < 0) { reason = "too big at this size"; return; }
    if (model.maxLevel - k > VoxelStore::kMaxLevel) { reason = "too small at this size"; return; }
    const double g = std::ldexp(1.0, -P);                          // origin grid (its largest voxel)
    const double u = std::ldexp(1.0, -(model.maxLevel - k));      // tight-bounds unit
    double tlo[3], thi[3];
    for (int a = 0; a < 3; ++a) { tlo[a] = model.lo[a] * u; thi[a] = model.hi[a] * u; }
    double corner[3];
    int roundMode[3] = {0, 0, 0};                                  // 0 nearest, +1 up, -1 down
    if (h.hit && h.t <= reach) {
        // Against the aimed face, centred on the aimed point.
        for (int a = 0; a < 3; ++a) {
            const double p = h.pos[a] - 1.0;
            if (h.normal[a] > 0)      { corner[a] = p - tlo[a]; roundMode[a] = +1; }
            else if (h.normal[a] < 0) { corner[a] = p - thi[a]; roundMode[a] = -1; }
            else corner[a] = p - 0.5 * (tlo[a] + thi[a]);
        }
    } else {
        // In the air ahead, far enough not to touch you.
        double ext2 = 0;
        for (int a = 0; a < 3; ++a) ext2 += (thi[a] - tlo[a]) * (thi[a] - tlo[a]);
        // (+ the grid's half diagonal: snapping may bring it that much closer)
        const double dist = std::max<double>(voidDistance, 0.5 * std::sqrt(ext2) + (keepOut ? 1.5 * keepOut[3] : 0.0) + 0.87 * g);
        for (int a = 0; a < 3; ++a) corner[a] = eye[a] - 1.0 + dir[a] * dist - 0.5 * (tlo[a] + thi[a]);
    }
    for (int a = 0; a < 3; ++a) {
        const double c = corner[a] / g;
        origin[a] = (int32_t)(roundMode[a] > 0 ? std::ceil(c - 1e-9) : roundMode[a] < 0 ? std::floor(c + 1e-9) : std::floor(c + 0.5));
    }
    // Check (cached while nothing changes).
    bool same = lastK == k && lastVersion == world.version() && lastDream == intoDream;
    for (int a = 0; a < 3; ++a) same = same && lastOrigin[a] == origin[a];
    for (int a = 0; a < 4; ++a) same = same && lastKeep[a] == (keepOut ? keepOut[a] : 0.0f);
    if (!same) {
        lastWhy = fits(world, keepOut, intoDream);
        lastK = k; lastVersion = world.version(); lastDream = intoDream;
        for (int a = 0; a < 3; ++a) lastOrigin[a] = origin[a];
        for (int a = 0; a < 4; ++a) lastKeep[a] = keepOut ? keepOut[a] : 0.0f;
    }
    valid = lastWhy == nullptr;
    if (!valid) reason = lastWhy;
}

int ObjectTool::place(World& world) {
    if (!valid) return 0;
    const int P = placeLevel();
    int n = 0;
    for (const auto& l : model.leaves) {
        const int lv = l.level - k;
        const int32_t local[3] = {l.x, l.y, l.z};
        Edit e;
        e.op = Edit::PLACE;
        e.box.level = lv;
        for (int a = 0; a < 3; ++a) e.box.lo[a] = e.box.hi[a] = (origin[a] << (lv - P)) + local[a];
        e.voxel = l.v;
        e.colorNoise = 0.0f;
        if (world.apply(e) > 0) ++n;
    }
    lastVersion = ~0ull;                     // the world changed: check again
    valid = false;
    return n;
}

void ObjectTool::fillGhost(Params& p) const {
    if (!valid) { p.ghostOn = 0; return; }
    const float g = std::ldexp(1.0f, -placeLevel());
    p.ghostOn = 1;
    for (int a = 0; a < 3; ++a) p.ghostOrigin[a] = 1.0f + origin[a] * g;
    p.ghostInvSize = std::ldexp(1.0f, model.cageLevel - k);
}
