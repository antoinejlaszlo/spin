#include "orientation.hpp"
#include <algorithm>
#include <cmath>

namespace {

V3f operator+(V3f a, V3f b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3f operator*(V3f a, float s) { return {a.x * s, a.y * s, a.z * s}; }
V3f neg(V3f a) { return {-a.x, -a.y, -a.z}; }
float dot(V3f a, V3f b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3f cross(V3f a, V3f b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
V3f normalize(V3f a) { float l = std::sqrt(dot(a, a)); return l > 0 ? a * (1.0f / l) : a; }

const V3f kAxes[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};

// Nearest of the 6 axis directions to v, optionally only among those
// perpendicular to `perp`.
V3f nearestAxis(V3f v, const V3f* perp = nullptr) {
    float best = -2.0f; V3f out = kAxes[0];
    for (const V3f& a : kAxes) {
        if (perp && std::fabs(dot(a, *perp)) > 0.5f) continue;
        float d = dot(a, v);
        if (d > best) { best = d; out = a; }
    }
    return out;
}

float smoothstep(float x) { x = std::clamp(x, 0.0f, 1.0f); return x * x * (3.0f - 2.0f * x); }

Quat qmul(const Quat& a, const Quat& b) {
    return {a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z, a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x, a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w};
}
Quat qconj(const Quat& q) { return {q.w, -q.x, -q.y, -q.z}; }

// Unit quaternion of a rotation matrix (row-major).
Quat quatFromMatrix(float m00, float m01, float m02, float m10, float m11, float m12, float m20, float m21, float m22) {
    Quat q;
    float tr = m00 + m11 + m22;
    if (tr > 0) {
        float s = std::sqrt(tr + 1.0f) * 2.0f;
        q = {0.25f * s, (m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s};
    } else if (m00 > m11 && m00 > m22) {
        float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
        q = {(m21 - m12) / s, 0.25f * s, (m01 + m10) / s, (m02 + m20) / s};
    } else if (m11 > m22) {
        float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
        q = {(m02 - m20) / s, (m01 + m10) / s, 0.25f * s, (m12 + m21) / s};
    } else {
        float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
        q = {(m10 - m01) / s, (m02 + m20) / s, (m12 + m21) / s, 0.25f * s};
    }
    return q;
}

// Rotates v about the unit axis a by angle (Rodrigues).
V3f rotateAxis(V3f v, V3f a, float angle) {
    float c = std::cos(angle), s = std::sin(angle);
    return v * c + cross(a, v) * s + a * (dot(a, v) * (1.0f - c));
}

// Body vectors stay exactly axis-aligned (remove float drift).
V3f snapAxis(V3f v) { return {std::round(v.x), std::round(v.y), std::round(v.z)}; }

} // namespace

// Rotation matrix with columns (right, up, -front): a right-handed frame.
Quat quatFromBasis(const V3f& f, const V3f& u) {
    V3f r = normalize(cross(f, u));
    V3f uu = cross(r, f);
    return quatFromMatrix(r.x, uu.x, -f.x, r.y, uu.y, -f.y, r.z, uu.z, -f.z);
}

void basisFromQuat(const Quat& q, V3f& f, V3f& u) {
    float w = q.w, x = q.x, y = q.y, z = q.z;
    // Column 1 (up) and column 2 (-front) of the rotation matrix.
    u = {2 * (x * y - w * z), 1 - 2 * (x * x + z * z), 2 * (y * z + w * x)};
    V3f back = {2 * (x * z + w * y), 2 * (y * z - w * x), 1 - 2 * (x * x + y * y)};
    f = neg(back);
}

Quat slerp(Quat a, Quat b, float t) {
    float d = a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z;
    if (d < 0) { b = {-b.w, -b.x, -b.y, -b.z}; d = -d; }   // shortest path
    if (d > 0.9995f) {
        Quat q = {a.w + (b.w - a.w) * t, a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
        float l = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
        return {q.w / l, q.x / l, q.y / l, q.z / l};
    }
    float th = std::acos(d), s = std::sin(th);
    float ka = std::sin((1 - t) * th) / s, kb = std::sin(t * th) / s;
    return {a.w * ka + b.w * kb, a.x * ka + b.x * kb, a.y * ka + b.y * kb, a.z * ka + b.z * kb};
}

void PlayerView::reset(float y, float p) {
    bR = {0, 0, 1}; bU = {0, 1, 0}; bF = {1, 0, 0};
    yaw = y; pitch = p;
    t = 1e9f;
}

void PlayerView::look(float dy, float dp) {
    yaw += dy;
    pitch = std::clamp(pitch + dp, -1.55f, 1.55f);
}

void PlayerView::physical(V3f& f, V3f& r, V3f& u) const {
    float cp = std::cos(pitch), sp = std::sin(pitch), cy = std::cos(yaw), sy = std::sin(yaw);
    f = normalize((bF * cy + bR * sy) * cp + bU * sp);
    r = normalize(bF * -sy + bR * cy);
    u = cross(r, f);
}

void PlayerView::visual(V3f& f, V3f& u) const {
    V3f r;
    physical(f, r, u);
    if (!transitioning()) return;
    Quat shown = qmul(slerp(off, Quat{}, smoothstep(t / duration)), quatFromBasis(f, u));
    basisFromQuat(shown, f, u);
}

bool PlayerView::turn(Turn turnKind) {
    if (transitioning()) return false;
    V3f f, r, u;
    physical(f, r, u);
    const Quat shownBefore = quatFromBasis(f, u);   // no transition is running: shown == physical

    // 1) Snap: front to its nearest axis, then right to its nearest axis
    //    perpendicular to the new front (this fixes the roll).
    V3f F = nearestAxis(f);
    V3f R = nearestAxis(r, &F);
    V3f U = cross(R, F);

    // 2) Rotate 90 degrees.
    switch (turnKind) {
    case PITCH_DOWN: { V3f nf = neg(U); U = F; F = nf; break; }   // nose down
    case PITCH_UP:   { V3f nf = U; U = neg(F); F = nf; break; }   // nose up
    case YAW_RIGHT:  { V3f nf = R; R = neg(F); F = nf; break; }
    case YAW_LEFT:   { V3f nf = neg(R); R = F; F = nf; break; }
    case ROLL_RIGHT: { V3f nu = R; R = neg(U); U = nu; break; }   // right side goes down
    case ROLL_LEFT:  { V3f nu = neg(R); R = U; U = nu; break; }
    }
    bR = R; bU = U; bF = F;
    yaw = 0; pitch = 0;
    physical(f, r, u);
    off = qmul(shownBefore, qconj(quatFromBasis(f, u)));   // ease from the old view
    t = 0;
    return true;
}

void PlayerView::landUpright() {
    const V3f up{0, 1, 0};
    float d = dot(bU, up);
    if (d > 0.999f) return;
    V3f vf, vu;
    visual(vf, vu);                      // ease from what is on screen right now
    const Quat shownBefore = quatFromBasis(vf, vu);
    // Smallest rotation taking the body's up to world up (upside down: flip
    // about the body's right axis, i.e. like a forward/backward somersault).
    V3f axis = d < -0.999f ? bR : normalize(cross(bU, up));
    float angle = std::acos(std::clamp(d, -1.0f, 1.0f));
    bR = snapAxis(rotateAxis(bR, axis, angle));
    bU = snapAxis(rotateAxis(bU, axis, angle));
    bF = snapAxis(rotateAxis(bF, axis, angle));
    V3f f, r, u;
    physical(f, r, u);                   // head yaw/pitch (mouse) is kept, relative to the new body
    off = qmul(shownBefore, qconj(quatFromBasis(f, u)));
    t = 0;
}

void PlayerView::applyRotation(const float m[9]) {
    auto rot = [&](V3f v) {
        return V3f{m[0] * v.x + m[1] * v.y + m[2] * v.z, m[3] * v.x + m[4] * v.y + m[5] * v.z,
                   m[6] * v.x + m[7] * v.y + m[8] * v.z};
    };
    bR = rot(bR); bU = rot(bU); bF = rot(bF);
    if (transitioning()) {               // keep the remaining easing in the rotated frame: off' = M off M^-1
        Quat q = quatFromMatrix(m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8]);
        off = qmul(qmul(q, off), qconj(q));
    }
}

void PlayerView::update(float dt) { if (t < 1e8f) t += dt; }

void PlayerView::walkDirections(float& fx, float& fz, float& rx, float& rz) const {
    V3f f, r, u;
    physical(f, r, u);
    fx = f.x; fz = f.z;
    if (fx * fx + fz * fz < 0.04f) {   // looking straight up or down: screen-up is "forward"
        fx = f.y < 0 ? u.x : -u.x;
        fz = f.y < 0 ? u.z : -u.z;
    }
    float l = std::sqrt(fx * fx + fz * fz);
    if (l < 1e-6f) { fx = 1; fz = 0; l = 1; }
    fx /= l; fz /= l;
    rx = r.x; rz = r.z;
    if (rx * rx + rz * rz < 0.04f) { rx = -fz; rz = fx; }   // rolled 90 degrees
    l = std::sqrt(rx * rx + rz * rz);
    rx /= l; rz /= l;
}
