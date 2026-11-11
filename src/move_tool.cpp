#include "move_tool.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <unordered_set>

namespace {

using Leaf = VoxelStore::Leaf;

uint64_t key(const Leaf& l) {
    return ((uint64_t)l.level << 51) | ((uint64_t)(uint32_t)l.x << 34) | ((uint64_t)(uint32_t)l.y << 17) | (uint64_t)(uint32_t)l.z;
}

void leafBox(const Leaf& l, double lo[3], double hi[3]) {
    const double s = 1.0 / (double)(1u << l.level);
    const int32_t c[3] = {l.x, l.y, l.z};
    for (int a = 0; a < 3; ++a) { lo[a] = c[a] * s; hi[a] = (c[a] + 1) * s; }
}

// Do two boxes share part of a face (touch along one axis, overlap with
// positive area in the other two)?
bool faceAdjacent(const Leaf& a, const Leaf& b) {
    double alo[3], ahi[3], blo[3], bhi[3];
    leafBox(a, alo, ahi); leafBox(b, blo, bhi);
    int overlap = 0, touch = 0;
    for (int k = 0; k < 3; ++k) {
        double o = std::min(ahi[k], bhi[k]) - std::max(alo[k], blo[k]);
        if (o > 1e-12) ++overlap;
        else if (o > -1e-12) ++touch;
    }
    return overlap == 2 && touch == 1;
}

bool rayBox(const float o[3], const float d[3], const float lo[3], const float hi[3], float& t) {
    float t0 = 0, t1 = 1e30f;
    for (int a = 0; a < 3; ++a) {
        if (std::fabs(d[a]) < 1e-9f) { if (o[a] < lo[a] || o[a] > hi[a]) return false; continue; }
        float ta = (lo[a] - o[a]) / d[a], tb = (hi[a] - o[a]) / d[a];
        if (ta > tb) std::swap(ta, tb);
        t0 = std::max(t0, ta); t1 = std::min(t1, tb);
        if (t0 > t1) return false;
    }
    t = t0;
    return true;
}

} // namespace

void MoveTool::bounds(float lo[3], float hi[3]) const {
    for (int a = 0; a < 3; ++a) { lo[a] = 1e30f; hi[a] = -1e30f; }
    for (const auto& l : sel) {
        double blo[3], bhi[3];
        leafBox(l, blo, bhi);
        for (int a = 0; a < 3; ++a) { lo[a] = std::min(lo[a], 1.0f + (float)blo[a]); hi[a] = std::max(hi[a], 1.0f + (float)bhi[a]); }
    }
}

// Arrow `ax`: shaft from the centre to past the object, and a knob at the tip.
// `pick` > 0 gives the (fatter) grab volume instead.
void MoveTool::arrow(int ax, float pick, float lo[3], float hi[3], float knobLo[3], float knobHi[3]) const {
    float blo[3], bhi[3];
    bounds(blo, bhi);
    float c[3];
    for (int a = 0; a < 3; ++a) c[a] = 0.5f * (blo[a] + bhi[a]);
    const float len = 0.5f * (bhi[ax] - blo[ax]) + cfg.arrowMargin;
    const float w = 0.5f * (pick > 0 ? pick : cfg.arrowWidth), k = 0.5f * cfg.knob;
    for (int a = 0; a < 3; ++a) {
        lo[a] = c[a] - w; hi[a] = c[a] + w;
        knobLo[a] = c[a] - k; knobHi[a] = c[a] + k;
    }
    hi[ax] = c[ax] + len;
    knobLo[ax] = c[ax] + len - k; knobHi[ax] = c[ax] + len + k;
}

void MoveTool::aim(const PickHit& h, const float eye[3], const float dir[3]) {
    target = h.hit && h.t <= cfg.reach;
    if (target) for (int a = 0; a < 3; ++a) aimPos[a] = h.pos[a] - h.normal[a] * 1e-5f;
    if (axis >= 0) return;                       // keep the grabbed arrow while dragging
    hover = -1;
    if (sel.empty()) return;
    float best = 1e30f;
    for (int ax = 0; ax < 3; ++ax) {
        float lo[3], hi[3], klo[3], khi[3], t;
        arrow(ax, cfg.pickWidth, lo, hi, klo, khi);
        for (int a = 0; a < 3; ++a) { klo[a] -= 0.5f * cfg.pickWidth; khi[a] += 0.5f * cfg.pickWidth; }
        if (rayBox(eye, dir, lo, hi, t) && t < best && t < 2 * cfg.reach) { best = t; hover = ax; }
        if (rayBox(eye, dir, klo, khi, t) && t < best && t < 2 * cfg.reach) { best = t; hover = ax; }
    }
}

bool MoveTool::press(World& world) {
    if (hover >= 0) { axis = hover; accum = 0; msg.clear(); return false; }
    if (!target) { msg = "Nothing to select"; return deselect(world); }
    return select(world, aimPos);
}

bool MoveTool::deselect(World& world) {
    axis = -1;
    if (sel.empty()) return false;
    world.setFlags(sel, 0, VOXEL_SELECTED);
    sel.clear();
    return true;
}

bool MoveTool::select(World& world, const float p[3]) {
    // The leaf under the reticle.
    std::vector<Leaf> found;
    double lo[3], hi[3];
    for (int a = 0; a < 3; ++a) { lo[a] = p[a] - 1.0 - 1e-9; hi[a] = p[a] - 1.0 + 1e-9; }
    world.leavesIn(lo, hi, found);
    if (found.empty()) { msg = "Nothing to select"; return deselect(world); }
    Leaf start = found[0];
    if (!(start.v.flags & VOXEL_BUILT)) {
        msg = "That is part of the scene: only built blocks can be moved";
        return deselect(world);
    }
    deselect(world);
    // Flood fill over face-connected built voxels.
    std::vector<Leaf> object;
    std::unordered_set<uint64_t> seen;
    std::deque<Leaf> todo;
    start.v.flags &= (uint8_t)~VOXEL_SELECTED;
    todo.push_back(start);
    seen.insert(key(start));
    std::vector<Leaf> near;
    while (!todo.empty()) {
        Leaf l = todo.front(); todo.pop_front();
        object.push_back(l);
        if (object.size() > cfg.maxLeaves) { msg = "That object is too big to move"; return false; }
        double blo[3], bhi[3];
        leafBox(l, blo, bhi);
        for (int a = 0; a < 3; ++a) { blo[a] -= 1e-9; bhi[a] += 1e-9; }
        near.clear();
        world.leavesIn(blo, bhi, near);
        for (Leaf n : near) {
            if (!(n.v.flags & VOXEL_BUILT) || seen.count(key(n)) || !faceAdjacent(l, n)) continue;
            n.v.flags &= (uint8_t)~VOXEL_SELECTED;
            seen.insert(key(n));
            todo.push_back(n);
        }
    }
    sel = std::move(object);
    coarse = VoxelStore::kMaxLevel;
    for (const auto& l : sel) coarse = std::min(coarse, l.level);
    world.setFlags(sel, VOXEL_SELECTED, 0);
    char buf[128];
    std::snprintf(buf, sizeof buf, "Selected an object of %zu voxel%s (moves by 1/%d of the world)",
                  sel.size(), sel.size() == 1 ? "" : "s", 1 << coarse);
    msg = buf;
    return true;
}

// Would the selection, shifted by `cells` cells of level `coarse` along ax,
// overlap anything that is not itself?
bool MoveTool::blocked(const World& world, int ax, int cells, const float* keepOut) const {
    const double g = 1.0 / (double)(1u << coarse);
    std::vector<Leaf> found;
    for (const auto& l : sel) {
        double lo[3], hi[3];
        leafBox(l, lo, hi);
        lo[ax] += cells * g; hi[ax] += cells * g;
        if (lo[ax] < -1e-12 || hi[ax] > 1.0 + 1e-12) return true;            // the world's edge
        double qlo[3], qhi[3];
        for (int a = 0; a < 3; ++a) { qlo[a] = lo[a] + 1e-9; qhi[a] = hi[a] - 1e-9; }
        found.clear();
        world.leavesIn(qlo, qhi, found);
        for (const auto& f : found) if (!(f.v.flags & VOXEL_SELECTED)) return true;   // someone else's voxel
        CellBox b = l.box();
        b.lo[ax] += cells << (l.level - coarse); b.hi[ax] = b.lo[ax];
        if (world.isLocked(b)) return true;                                   // doors, start box
        if (keepOut) {                                                        // the player
            float d2 = 0;
            for (int a = 0; a < 3; ++a) {
                float c = std::clamp(keepOut[a], 1.0f + (float)lo[a], 1.0f + (float)hi[a]);
                d2 += (keepOut[a] - c) * (keepOut[a] - c);
            }
            if (d2 < keepOut[3] * keepOut[3]) return true;
        }
    }
    return false;
}

int MoveTool::moveBy(World& world, int ax, int steps, const float* keepOut) {
    if (sel.empty() || steps == 0 || ax < 0 || ax > 2) return 0;
    const int dir = steps > 0 ? 1 : -1;
    int ok = 0;
    while (ok != steps && !blocked(world, ax, ok + dir, keepOut)) ok += dir;   // every step, so nothing is jumped over
    if (ok == 0) return 0;
    // Remove the object, then write it back shifted (exact colours and flags).
    for (const auto& l : sel) {
        Edit e; e.op = Edit::REMOVE; e.box = l.box();
        world.apply(e);
    }
    for (auto& l : sel) {
        int32_t* c = ax == 0 ? &l.x : ax == 1 ? &l.y : &l.z;
        *c += ok << (l.level - coarse);
        Edit e; e.op = Edit::PLACE; e.box = l.box(); e.voxel = l.v; e.colorNoise = 0.0f;
        world.apply(e);
    }
    world.setFlags(sel, VOXEL_SELECTED, 0);
    return ok;
}

int MoveTool::drag(float dx, float dy, const float eye[3], const float right[3], const float up[3],
                   World& world, const float* keepOut) {
    if (axis < 0 || sel.empty()) return 0;
    float lo[3], hi[3];
    bounds(lo, hi);
    float d2 = 0;
    for (int a = 0; a < 3; ++a) { float c = 0.5f * (lo[a] + hi[a]) - eye[a]; d2 += c * c; }
    const float perPixel = std::max(0.02f, std::sqrt(d2)) * cfg.mouseSens;   // octree units per pixel at the object
    // The axis as seen on screen (x right, y down).
    float sx = right[axis], sy = -up[axis], sl = std::sqrt(sx * sx + sy * sy);
    float along;
    if (sl < 0.25f) along = -dy * perPixel;                       // axis points at you: up/down moves it
    else along = (dx * sx + dy * sy) / sl * perPixel / sl;
    accum += along;
    const float g = step();
    int want = (int)(accum / g);                                  // whole steps, toward zero
    if (want == 0) return 0;
    int moved = moveBy(world, axis, want, keepOut);
    accum = moved == want ? accum - moved * g : 0.0f;             // blocked: drop the rest
    return moved;
}

int MoveTool::ghosts(Overlay* out, int max) const {
    if (sel.empty()) return 0;
    int n = 0;
    if (max >= 7) {                         // the selection's box, outlined (cage voxels are not tinted)
        Overlay& o = out[n++];
        bounds(o.lo, o.hi);
        o.color[0] = 1.0f; o.color[1] = 0.8f; o.color[2] = 0.3f; o.color[3] = 0.0f;
        o.style = OVERLAY_LINES_HOST;
    }
    const float rgb[3][3] = {{1.0f, 0.25f, 0.22f}, {0.3f, 0.95f, 0.35f}, {0.3f, 0.5f, 1.0f}};
    for (int ax = 0; ax < 3 && n + 1 < max; ++ax) {
        float lo[3], hi[3], klo[3], khi[3];
        arrow(ax, 0, lo, hi, klo, khi);
        const bool hot = ax == axis || (axis < 0 && ax == hover);
        float c[3];
        for (int i = 0; i < 3; ++i) c[i] = hot ? 0.5f + 0.5f * rgb[ax][i] : rgb[ax][i];
        for (int k = 0; k < 2; ++k) {
            Overlay& o = out[n++];
            for (int a = 0; a < 3; ++a) { o.lo[a] = k ? klo[a] : lo[a]; o.hi[a] = k ? khi[a] : hi[a]; }
            o.color[0] = c[0]; o.color[1] = c[1]; o.color[2] = c[2]; o.color[3] = hot ? 0.95f : 0.75f;
            o.style = OVERLAY_FILL_HOST | OVERLAY_ONTOP_HOST;
        }
    }
    return n;
}
