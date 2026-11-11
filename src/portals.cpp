#include "portals.hpp"
#include <algorithm>
#include <cmath>

namespace {
Vec3 v3(const float* a) { return {a[0], a[1], a[2]}; }
float dot3(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 mad(Vec3 a, Vec3 b, float s) { return {a.x + b.x * s, a.y + b.y * s, a.z + b.z * s}; }
} // namespace

PortalFrame Portals::frameFor(const CellBox& w, int ua, int us, int na, int ns) {
    const float s = 1.0f / (float)(1 << w.level);
    const int ra = 3 - ua - na;                // the remaining axis: width
    PortalFrame f{};
    float face = 1.0f + (ns > 0 ? (w.hi[na] + 1) : w.lo[na]) * s;
    f.c[na] = face + ns * 1e-3f * s;           // just in front of the wall face
    f.c[ua] = 1.0f + (w.lo[ua] + w.hi[ua] + 1) * 0.5f * s;
    f.c[ra] = 1.0f + (w.lo[ra] + w.hi[ra] + 1) * 0.5f * s;
    f.n[na] = (float)ns;
    f.u[ua] = (float)us;
    // r = u x n
    f.r[0] = f.u[1] * f.n[2] - f.u[2] * f.n[1];
    f.r[1] = f.u[2] * f.n[0] - f.u[0] * f.n[2];
    f.r[2] = f.u[0] * f.n[1] - f.u[1] * f.n[0];
    f.halfW = (w.hi[ra] - w.lo[ra] + 1) * 0.5f * s;
    f.halfH = (w.hi[ua] - w.lo[ua] + 1) * 0.5f * s;
    return f;
}

bool Portals::create(World& world, const CellBox& wall, int ua, int us, int axis, int sign, const float* keepOut) {
    int slot = freeSlot();
    if (slot < 0 || ua < 0 || ua > 2 || axis < 0 || axis > 2 || ua == axis || wall.lo[axis] != wall.hi[axis]) return false;
    PortalFrame f = frameFor(wall, ua, us, axis, sign);
    const PortalDoor& other = door[1 - slot];
    if (other.exists && (std::fabs(other.frame.halfW - f.halfW) > 1e-6f || std::fabs(other.frame.halfH - f.halfH) > 1e-6f))
        return false;                                  // both doors must be the same size
    if (world.isLocked(wall)) return false;
    if (keepOut) {                                     // never wall the player in
        const float s = 1.0f / (float)(1 << wall.level);
        float d2 = 0;
        for (int a = 0; a < 3; ++a) {
            float lo = 1.0f + wall.lo[a] * s, hi = 1.0f + (wall.hi[a] + 1) * s;
            float c = std::clamp(keepOut[a], lo, hi);
            d2 += (keepOut[a] - c) * (keepOut[a] - c);
        }
        if (d2 < keepOut[3] * keepOut[3]) return false;
    }
    Edit e;
    e.op = Edit::PLACE;
    e.box = wall;
    e.voxel.color = kWallColor;
    e.voxel.flags = 0;
    world.apply(e);
    PortalDoor& d = door[slot];
    d.exists = true;
    d.wall = wall;
    d.upAxis = ua;
    d.upSign = us;
    d.normalAxis = axis;
    d.normalSign = sign;
    d.frame = f;
    d.lockId = world.lock(wall);
    if (linked()) open = 1 - slot;                    // the door that was already there opens first
    return true;
}

void Portals::remove(World& world, int i) {
    if (i < 0 || i > 1 || !door[i].exists) return;
    Edit e;
    e.op = Edit::REMOVE;
    e.box = door[i].wall;
    e.bypassLocks = true;
    world.apply(e);
    world.unlock(door[i].lockId);
    door[i] = PortalDoor{};
}

int Portals::doorAt(const float p[3]) const {
    for (int i = 0; i < 2; ++i) {
        if (!door[i].exists) continue;
        const CellBox& w = door[i].wall;
        const float s = 1.0f / (float)(1 << w.level), e = 1e-4f;
        bool in = true;
        for (int a = 0; a < 3; ++a)
            in &= p[a] >= 1.0f + w.lo[a] * s - e && p[a] <= 1.0f + (w.hi[a] + 1) * s + e;
        if (in) return i;
    }
    return -1;
}

void Portals::fillParams(Params& p) const {
    p.portalOpen = linked() ? 1 : 0;
    if (!linked()) return;
    p.portalIn = door[open].frame;
    p.portalOut = door[1 - open].frame;
}

Vec3 Portals::mapDir(Vec3 d) const {
    const PortalFrame& in = door[open].frame;
    const PortalFrame& out = door[1 - open].frame;
    float x = dot3(d, v3(in.r)), y = dot3(d, v3(in.u)), z = dot3(d, v3(in.n));
    Vec3 r{0, 0, 0};
    r = mad(r, v3(out.r), -x);
    r = mad(r, v3(out.u), y);
    r = mad(r, v3(out.n), -z);
    return r;
}

Vec3 Portals::mapPoint(Vec3 p) const {
    const PortalFrame& in = door[open].frame;
    Vec3 q = mapDir(sub(p, v3(in.c)));
    const PortalFrame& out = door[1 - open].frame;
    return {out.c[0] + q.x, out.c[1] + q.y, out.c[2] + q.z};
}

void Portals::rotation(float m[9]) const {
    const Vec3 e[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    for (int c = 0; c < 3; ++c) {
        Vec3 v = mapDir(e[c]);
        m[0 * 3 + c] = v.x; m[1 * 3 + c] = v.y; m[2 * 3 + c] = v.z;
    }
}

bool Portals::ignoreBox(const VoxelWorld::Box& b, const Vec3& centre) const {
    if (!linked()) return false;
    const PortalFrame& f = door[open].frame;
    Vec3 q = sub(centre, v3(f.c));
    if (dot3(q, v3(f.n)) < 0.0f) return false;                       // behind the doorway
    if (std::fabs(dot3(q, v3(f.r))) >= f.halfW || std::fabs(dot3(q, v3(f.u))) >= f.halfH) return false;
    // Extent of the box along each door axis, relative to the doorway centre
    // (the axes are world axes, so the box corners give the exact interval).
    auto span = [&](const float* axis, float& lo, float& hi) {
        Vec3 a = v3(axis);
        float p = dot3(sub(b.lo, v3(f.c)), a), r = dot3(sub(b.hi, v3(f.c)), a);
        lo = std::min(p, r); hi = std::max(p, r);
    };
    const float tiny = 1e-7f;
    float nlo, nhi, rlo, rhi, ulo, uhi;
    span(f.n, nlo, nhi); span(f.r, rlo, rhi); span(f.u, ulo, uhi);
    if (nhi > tiny) return false;                                    // not entirely behind the doorway
    return rhi > -f.halfW + tiny && rlo < f.halfW - tiny && uhi > -f.halfH + tiny && ulo < f.halfH - tiny;
}

bool Portals::crosses(const Vec3& a, const Vec3& b) const {
    if (!linked()) return false;
    const PortalFrame& f = door[open].frame;
    float za = dot3(sub(a, v3(f.c)), v3(f.n)), zb = dot3(sub(b, v3(f.c)), v3(f.n));
    if (!(za >= 0.0f && zb < 0.0f)) return false;
    float t = za / (za - zb);
    Vec3 p = {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
    Vec3 q = sub(p, v3(f.c));
    return std::fabs(dot3(q, v3(f.r))) <= f.halfW && std::fabs(dot3(q, v3(f.u))) <= f.halfH;
}
