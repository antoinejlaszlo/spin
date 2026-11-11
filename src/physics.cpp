#include "physics.hpp"
#include "portals.hpp"
#include <algorithm>
#include <cmath>

namespace {
Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float len2(Vec3 a) { return dot(a, a); }
Vec3 normalize(Vec3 a) { float l = std::sqrt(dot(a, a)); return l > 1e-9f ? a * (1.0f / l) : a; }
Vec3 clampToBox(Vec3 p, const VoxelWorld::Box& b) {
    return {std::clamp(p.x, b.lo.x, b.hi.x), std::clamp(p.y, b.lo.y, b.hi.y), std::clamp(p.z, b.lo.z, b.hi.z)};
}
const Vec3 kWorldUp{0, 1, 0};
} // namespace

// ---------------------------------------------------------------------------
// Octree box query: walk down only into children that overlap the query box.
// Large solid blocks come back as one big box, so the cost does not depend on
// the octree depth.
// ---------------------------------------------------------------------------
void VoxelWorld::query(const Vec3& lo, const Vec3& hi, std::vector<Box>& out) const {
    out.clear();
    if (oct.nodes.empty()) return;
    struct Item { uint32_t node; float x, y, z, size; };
    Item stack[256];
    int sp = 0;
    stack[sp++] = {0, 1.0f, 1.0f, 1.0f, 1.0f};
    const bool hasFlags = oct.flags.size() == oct.colors.size();
    while (sp > 0) {
        Item it = stack[--sp];
        uint32_t first = oct.nodes[2 * it.node], masks = oct.nodes[2 * it.node + 1];
        float h = it.size * 0.5f;
        uint32_t k = first;
        for (int s = 0; s < 8; ++s) {
            if (!(masks & (1u << s))) continue;
            uint32_t child = k++;
            float cx = it.x + ((s & 1) ? h : 0.0f);
            float cy = it.y + ((s & 2) ? h : 0.0f);
            float cz = it.z + ((s & 4) ? h : 0.0f);
            if (cx > hi.x || cx + h < lo.x || cy > hi.y || cy + h < lo.y || cz > hi.z || cz + h < lo.z) continue;
            if ((masks & (1u << (s + 8))) || h < 4e-6f) {   // a leaf (or as deep as floats go: nested cage voxels)
                bool grip = hasFlags && (oct.flags[child] & VOXEL_GRIP);
                out.push_back({{cx, cy, cz}, {cx + h, cy + h, cz + h}, grip});
            } else if (sp < 256) {
                stack[sp++] = {child, cx, cy, cz, h};
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Sphere vs voxel boxes. The closest point of a box to the centre is just the
// centre clamped to the box, so every contact is exact.
// ---------------------------------------------------------------------------
bool Player::overlaps() {
    const float r = cfg.radius;
    world.query(pos - Vec3{r, r, r}, pos + Vec3{r, r, r}, boxes);
    for (const auto& b : boxes) {
        if (portals && portals->ignoreBox(b, pos)) continue;
        if (len2(pos - clampToBox(pos, b)) < r * r * 0.999f) return true;
    }
    return false;
}

// Pushes the sphere out of every overlapping box (a few Gauss-Seidel passes).
//  - Grip landing: not yet gripping, the box has grip, and it touches the
//    player's bottom (contact normal close to the body's up). From now on
//    S = body up and world gravity is off.
//  - Support: the contact point is "below" the centre along S (a floor, or
//    the edge of a ledge lower than the radius). The sphere is moved along S
//    until it rests exactly on that point, and motion into it stops. Walking
//    into a low ledge therefore rolls the sphere over it; no sliding on slopes.
//  - Otherwise wall / ceiling: pushed out along the normal, and the velocity
//    going into the surface is removed (slide).
void Player::resolve(bool& hasSupport, bool& gripSupport) {
    const float r = cfg.radius, m = 0.5f * r;   // margin: also catch boxes we may be pushed into
    world.query(pos - Vec3{r + m, r + m, r + m}, pos + Vec3{r + m, r + m, r + m}, boxes);
    for (int pass = 0; pass < 4; ++pass) {
        bool moved = false;
        for (const auto& b : boxes) {
            if (portals && portals->ignoreBox(b, pos)) continue;   // stepping into the open door
            Vec3 c = clampToBox(pos, b);
            Vec3 d = pos - c;
            float d2 = len2(d);
            if (d2 >= r * r) continue;
            moved = true;
            float dist = std::sqrt(d2);

            if (!gripping && b.grip && dist > 1e-6f && dot(d, bodyUp) > cfg.gripAlign * dist) {
                gripping = true;
                support = bodyUp;
            }
            const Vec3 S = support;

            if (dist <= 1e-6f) {
                // Centre inside the box (only after a teleport): leave along S.
                float a = dot(pos, S);
                float lo = dot(b.lo, S), hi = dot(b.hi, S);
                pos = pos + S * ((std::max(lo, hi) - a) + r);
                continue;
            }
            float along = dot(d, S);
            Vec3 lat = d - S * along;
            float lat2 = len2(lat);
            if (along > 1e-6f && lat2 < r * r) {
                pos = c + lat + S * std::sqrt(r * r - lat2);   // rest exactly on the contact point
                float vn = dot(vel, S);
                if (vn < 0) vel = vel - S * vn;
                hasSupport = true;
                if (b.grip || gripAny) gripSupport = true;
            } else {
                Vec3 n = d * (1.0f / dist);
                pos = pos + n * (r - dist);
                float vn = dot(vel, n);
                if (vn < 0) vel = vel - n * vn;
            }
        }
        if (!moved) break;
    }
}

void Player::respawn() {
    ++respawns;
    pos = spawnPos;
    vel = {};
    grounded = jumped = gripping = gripAny = false;
    support = kWorldUp;
    coyote = 0;
    // Never start inside solid voxels: rise until the sphere is free.
    for (int i = 0; i < 400 && overlaps(); ++i) pos.y += 0.5f * cfg.radius;
}

void Player::update(float dt, const PlayerInput& in) {
    dt = std::min(dt, 0.1f);
    bodyUp = in.hasView ? normalize(in.bodyUp) : kWorldUp;
    float speed = std::sqrt(len2(vel)) + cfg.walkSpeed * cfg.sprintMul;
    // Small substeps: never move more than ~30% of the radius per step (no tunnelling).
    int n = (int)std::ceil(std::max(dt * 240.0f, speed * dt / (0.3f * cfg.radius)));
    n = std::clamp(n, 1, 64);
    PlayerInput local = in;               // rotated along with us if we cross a portal
    for (int i = 0; i < n; ++i) step(dt / n, local);
    if (pos.y < 0.8f) respawn();           // fell off the world
}

void Player::step(float dt, PlayerInput& in) {
    Vec3 S = support;

    // Walking directions on the surface (the plane perpendicular to S).
    Vec3 f, r;
    if (in.hasView) {
        f = in.viewF - S * dot(in.viewF, S);
        if (len2(f) < 0.04f) {   // looking straight at / away from the surface: screen-up is forward
            Vec3 u = dot(in.viewF, S) < 0 ? in.viewU : in.viewU * -1.0f;
            f = u - S * dot(u, S);
        }
        f = normalize(f);
        r = in.viewR - S * dot(in.viewR, S);
        if (len2(r) < 0.04f) r = cross(f, S);   // rolled 90 degrees
        r = normalize(r);
    } else {
        f = {std::cos(in.yaw), 0, std::sin(in.yaw)};
        r = {-f.z, 0, f.x};
    }
    Vec3 wish = f * in.forward + r * in.strafe;
    float wl = std::sqrt(len2(wish));
    if (wl > 1.0f) wish = wish * (1.0f / wl);
    wish = wish * (cfg.walkSpeed * (in.sprint ? cfg.sprintMul : 1.0f));

    // Steer the tangential velocity; the part along S belongs to jumps and gravity.
    float vn = dot(vel, S);
    Vec3 vt = vel - S * vn;
    if (grounded) {                       // steering only on a surface: in the air, pure inertia + gravity
        float k = std::min(1.0f, cfg.groundAccel * dt);
        vt = vt + (wish - vt) * k;
    }
    vel = vt + S * vn;

    coyote = grounded ? 0.1f : std::max(0.0f, coyote - dt);
    if (in.jump && coyote > 0.0f && vn <= 0.01f) {
        vel = vel + S * (cfg.jumpSpeed - vn);   // jump away from the surface, keep the momentum
        coyote = 0;
        grounded = false;
        jumped = true;
        if (gripping) { gripping = gripAny = false; support = kWorldUp; }   // world gravity again
    }

    // Gravity along -S (world gravity, or the hold toward the gripped surface).
    S = support;
    vel = vel - S * (cfg.gravity * dt);
    float fall = -dot(vel, S);
    if (fall > cfg.maxFall) vel = vel + S * (fall - cfg.maxFall);

    const bool wasGrounded = grounded;
    const Vec3 start = pos;
    pos = pos + vel * dt;
    // Through the open door: the centre (the camera) crossed its doorway.
    // Checked right after moving, before collisions: on the far side of the
    // doorway the entrance door's own wall must not push us around.
    if (portals && portals->crosses(start, pos)) {
        float m[9];
        portals->rotation(m);
        auto rot = [&](Vec3 v) {
            return Vec3{m[0] * v.x + m[1] * v.y + m[2] * v.z, m[3] * v.x + m[4] * v.y + m[5] * v.z,
                        m[6] * v.x + m[7] * v.y + m[8] * v.z};
        };
        pos = portals->mapPoint(pos);
        vel = rot(vel);
        bodyUp = rot(bodyUp);
        in.viewF = rot(in.viewF); in.viewR = rot(in.viewR); in.viewU = rot(in.viewU); in.bodyUp = rot(in.bodyUp);
        // Arriving: if you walked in while supported (on the ground, or
        // gripping a wall or ceiling) you stay on your feet relative to the
        // exit door: on a door standing on a wall or ceiling you now grip that
        // surface (any voxels); on one standing on the ground you just stand.
        // Jumping (or falling) through keeps you in the air, world gravity.
        if ((grounded || gripping) && !jumped) {
            Vec3 ns = rot(support);
            if (ns.y > 0.99f) { gripping = gripAny = false; support = kWorldUp; }
            else { gripping = gripAny = true; support = ns; }
        } else {
            gripping = gripAny = false;
            support = kWorldUp;
        }
        portals->swapOpen();             // the door you came through closes, the other opens
        ++teleports;
        float acc[9];                    // pendingRot = m * pendingRot
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c)
                acc[r * 3 + c] = m[r * 3] * pendingRot[c] + m[r * 3 + 1] * pendingRot[3 + c] + m[r * 3 + 2] * pendingRot[6 + c];
        std::copy(acc, acc + 9, pendingRot);
        hasPendingRot = true;
    }

    bool sup = false, gripSup = false;
    resolve(sup, gripSup);

    if (gripping && !gripSup) {
        // Lost contact with the grip surface (walked off its edge, or onto a
        // normal voxel): try to stay on it over a small drop, else let go.
        const Vec3 keep = pos;
        bool s2 = false, g2 = false;
        for (int i = 0; i < 3 && !g2; ++i) {
            pos = pos - support * (cfg.snapDown / 3);
            resolve(s2, g2);
        }
        if (g2) { sup = gripSup = true; }
        else {
            pos = keep;
            gripping = gripAny = false;
            support = kWorldUp;          // keeps the velocity built up on the wall
            sup = false;
        }
    } else if (!gripping && wasGrounded && !sup && dot(vel, support) <= 0.0f) {
        // Stay on the ground when walking down small drops, instead of hopping.
        const Vec3 keep = pos;
        bool s2 = false, g2 = false;
        for (int i = 0; i < 3 && !s2; ++i) {
            pos = pos - support * (cfg.snapDown / 3);
            resolve(s2, g2);
        }
        if (s2) sup = true; else pos = keep;
    }
    grounded = sup;
    if (grounded && dot(vel, support) <= 0.0f) jumped = false;

}

bool Player::takeRotation(float m[9]) {
    if (!hasPendingRot) return false;
    std::copy(pendingRot, pendingRot + 9, m);
    const float id[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    std::copy(id, id + 9, pendingRot);
    hasPendingRot = false;
    return true;
}

bool Player::setScale(float k, bool force) {
    if (k == curScale) return true;
    if (curScale == 1.0f) unit = cfg;
    const PlayerConfig old = cfg;
    const Vec3 oldPos = pos;
    cfg = unit;
    cfg.radius *= k; cfg.walkSpeed *= k; cfg.gravity *= k; cfg.jumpSpeed *= k;
    cfg.maxFall *= k; cfg.snapDown *= k;
    pos = pos + support * (cfg.radius - old.radius);   // keep the bottom of the sphere in place
    if (!force && overlaps()) { cfg = old; pos = oldPos; return false; }
    vel = vel * (k / curScale);
    curScale = k;
    return true;
}
