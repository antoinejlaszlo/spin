#include "start_tool.hpp"
#include <algorithm>
#include <cmath>

namespace {

const float kStartColor[3] = {0.35f, 1.0f, 0.45f};

void cellOverlay(const CellBox& b, const float rgb[3], float alpha, int style, Overlay& o) {
    const float s = 1.0f / (float)(1 << b.level);
    for (int a = 0; a < 3; ++a) { o.lo[a] = 1.0f + b.lo[a] * s; o.hi[a] = 1.0f + (b.hi[a] + 1) * s; }
    o.color[0] = rgb[0]; o.color[1] = rgb[1]; o.color[2] = rgb[2]; o.color[3] = alpha;
    o.style = style;
}

// A small plate on the box's front face: shows which way the player will face.
void noseOverlay(const CellBox& b, float yaw, const float rgb[3], Overlay& o) {
    cellOverlay(b, rgb, 0.8f, OVERLAY_FILL_HOST, o);
    const float s = 1.0f / (float)(1 << b.level);
    float fx = std::cos(yaw), fz = std::sin(yaw);
    int a = std::fabs(fx) >= std::fabs(fz) ? 0 : 2, side = 2 - a;
    float sign = (a == 0 ? fx : fz) > 0 ? 1.0f : -1.0f;
    float face = sign > 0 ? o.hi[a] : o.lo[a];
    o.lo[a] = face - (sign > 0 ? 0.0f : 0.3f * s);
    o.hi[a] = face + (sign > 0 ? 0.3f * s : 0.0f);
    float cs = 0.5f * (o.lo[side] + o.hi[side]), hs = 0.25f * (o.hi[side] - o.lo[side]);
    o.lo[side] = cs - hs; o.hi[side] = cs + hs;
    float h = o.hi[1] - o.lo[1];
    o.lo[1] += 0.45f * h; o.hi[1] -= 0.2f * h;
}

bool rayBox(const float e[3], const float d[3], const CellBox& b, float& t) {
    const float s = 1.0f / (float)(1 << b.level);
    float t0 = 0, t1 = 1e30f;
    for (int a = 0; a < 3; ++a) {
        float lo = 1.0f + b.lo[a] * s, hi = 1.0f + (b.hi[a] + 1) * s;
        if (std::fabs(d[a]) < 1e-9f) { if (e[a] < lo || e[a] > hi) return false; continue; }
        float ta = (lo - e[a]) / d[a], tb = (hi - e[a]) / d[a];
        if (ta > tb) std::swap(ta, tb);
        t0 = std::max(t0, ta); t1 = std::min(t1, tb);
        if (t0 > t1) return false;
    }
    t = t0;
    return true;
}

} // namespace

void StartTool::aim(const PickHit& h, const float eye[3], const float dir[3], const Level& lvl, int level, float radius) {
    candidate = valid = hover = false;
    float tb;
    if (lvl.start.set && rayBox(eye, dir, lvl.start.box, tb) && tb <= reach && (!h.hit || tb < h.t)) {
        hover = true;                       // left click removes it
        return;
    }
    if (!h.hit || h.t > reach) return;
    // Face the way you look, snapped to the nearest horizontal axis.
    yaw = std::fabs(dir[0]) >= std::fabs(dir[2]) ? (dir[0] > 0 ? 0.0f : 3.14159265f)
                                                 : (dir[2] > 0 ? 1.5707963f : -1.5707963f);
    const float n = (float)(1 << level);
    const int w = std::max(1, (int)std::ceil(2.0f * radius * n - 1e-4f));   // box side in cells
    int32_t c[3];
    for (int a = 0; a < 3; ++a) c[a] = (int32_t)std::floor((h.pos[a] + h.normal[a] * 1e-5f - 1.0f) * n);
    cand = CellBox{};
    cand.level = level;
    for (int a : {0, 2}) { cand.lo[a] = c[a] - (w - 1) / 2; cand.hi[a] = cand.lo[a] + w - 1; }
    cand.lo[1] = c[1]; cand.hi[1] = c[1] + w - 1;
    candidate = true;
    valid = h.normal[1] == 1 && lvl.startFree(cand);   // floors only
}

bool StartTool::place(Level& lvl) {
    return candidate && valid && lvl.setStart(cand, yaw);
}

bool StartTool::remove(Level& lvl) {
    if (!hover || !lvl.start.set) return false;
    lvl.clearStart();
    hover = false;
    return true;
}

int StartTool::ghosts(const Level& lvl, Overlay* out, int max) const {
    const float red[3] = {1.0f, 0.25f, 0.2f};
    if (hover && lvl.start.set && max >= 1) {          // left click removes it
        cellOverlay(lvl.start.box, red, 0.35f, OVERLAY_FILL_HOST, out[0]);
        return 1;
    }
    if (!candidate || max < 2) return 0;
    const float* c = valid ? kStartColor : red;
    cellOverlay(cand, c, 0.3f, OVERLAY_FILL_HOST, out[0]);
    noseOverlay(cand, yaw, c, out[1]);
    return 2;
}

int StartTool::marker(const StartPoint& s, Overlay* out, int max) {
    if (!s.set || max < 2) return 0;
    cellOverlay(s.box, kStartColor, 0.12f, OVERLAY_FILL_HOST, out[0]);
    noseOverlay(s.box, s.yaw, kStartColor, out[1]);
    return 2;
}
