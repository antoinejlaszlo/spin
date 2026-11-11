#include "build_tool.hpp"
#include <algorithm>
#include <cmath>

namespace {

uint32_t rgb8(float r, float g, float b) {
    auto c = [](float v) { return (uint32_t)std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f); };
    return c(r) | (c(g) << 8) | (c(b) << 16) | (255u << 24);
}

uint32_t hsv(float h, float s, float v) {
    float c = v * s, x = c * (1 - std::fabs(std::fmod(h / 60.0f, 2.0f) - 1)), m = v - c;
    float r = 0, g = 0, b = 0;
    if (h < 60) { r = c; g = x; } else if (h < 120) { r = x; g = c; } else if (h < 180) { g = c; b = x; }
    else if (h < 240) { g = x; b = c; } else if (h < 300) { r = x; b = c; } else { r = c; b = x; }
    return rgb8(r + m, g + m, b + m);
}

void unpack(uint32_t c, float out[3]) {
    // Display-space approximation of a lit block (the renderer applies gamma 2).
    for (int i = 0; i < 3; ++i) out[i] = std::sqrt(((c >> (8 * i)) & 255u) / 255.0f);
}

} // namespace

BuildTool::BuildTool(int baseLevel) : base(baseLevel) {
    // Row 1: neutrals and earth tones. Rows 2-4: 8 hues, vivid / soft / dark.
    colors = {rgb8(0.95f, 0.95f, 0.95f), rgb8(0.72f, 0.72f, 0.74f), rgb8(0.48f, 0.48f, 0.50f), rgb8(0.26f, 0.26f, 0.28f),
              rgb8(0.08f, 0.08f, 0.09f), rgb8(0.45f, 0.30f, 0.18f), rgb8(0.76f, 0.60f, 0.42f), rgb8(0.92f, 0.86f, 0.70f)};
    const float hues[8] = {0, 28, 52, 115, 170, 215, 272, 320};
    for (float h : hues) colors.push_back(hsv(h, 0.85f, 0.92f));
    for (float h : hues) colors.push_back(hsv(h, 0.45f, 0.85f));
    for (float h : hues) colors.push_back(hsv(h, 0.80f, 0.45f));
}

void BuildTool::select(int i) { if (i >= 0 && i < (int)colors.size()) sel = i; }

void BuildTool::setAvailable(unsigned mask) {
    avail = mask | (1u << TOOL_BLOCKS) | (1u << TOOL_NONE);
    if (!available(current)) { cancel(); current = TOOL_BLOCKS; }
}

void BuildTool::nextTool() {
    for (int i = 1; i <= TOOL_COUNT; ++i) {
        Tool t = (Tool)((current + i) % TOOL_COUNT);
        if (available(t)) { setTool(t); return; }
    }
}

bool cellsInside(const CellBox& b, int level, int32_t lo[3], int32_t hi[3]) {
    for (int a = 0; a < 3; ++a) {
        int64_t l = b.lo[a], h = (int64_t)b.hi[a] + 1;      // [l, h) at b.level
        if (level >= b.level) { l <<= (level - b.level); h <<= (level - b.level); }
        else {
            const int64_t d = 1ll << (b.level - level);
            l = (l + d - 1) / d;                              // whole cells only
            h = h / d;
        }
        if (h <= l) return false;
        lo[a] = (int32_t)l; hi[a] = (int32_t)(h - 1);
    }
    return true;
}

int BuildTool::boxOf(const int32_t c[3]) const {
    if (!allowed) return 0;
    for (size_t i = 0; i < allowed->size(); ++i) {
        int32_t lo[3], hi[3];
        if (!cellsInside((*allowed)[i], level(), lo, hi)) continue;
        if (c[0] >= lo[0] && c[0] <= hi[0] && c[1] >= lo[1] && c[1] <= hi[1] && c[2] >= lo[2] && c[2] <= hi[2])
            return (int)i;
    }
    return -1;
}

void BuildTool::setSizeStep(int s) {
    s = std::clamp(s, 0, cfg.sizeSteps - 1);
    if (s != step) cancel();     // a drag is defined on one grid
    step = s;
}

Voxel BuildTool::voxel() const {
    Voxel v;
    v.color = colors[sel];
    v.flags = (uint8_t)(VOXEL_BUILT | (gripOn ? VOXEL_GRIP : 0));
    return v;
}

// The targeted cells at the selected size: the one just inside the face under
// the crosshair (remove), and the one just outside it (place). When the face
// is not on the selected grid (e.g. a small block's face, big blocks
// selected), the outside cell could contain the face itself; it is then moved
// one more cell out, so placing never swallows the block you aimed at.
void BuildTool::aim(const PickHit& h, const float* eye, const float* dir) {
    target = h.hit && h.t <= cfg.reach;
    voidTarget = false;
    const float n = (float)(1 << level());
    if (!target) {
        if (!eye || !dir) return;
        // Nothing in reach: the cell at a fixed distance along the view.
        const int32_t cells = 1 << level();
        for (int a = 0; a < 3; ++a) {
            placeCell[a] = (int32_t)std::floor((eye[a] + dir[a] * cfg.voidDistance - 1.0f) * n);
            if (placeCell[a] < 0 || placeCell[a] >= cells) return;   // outside the world
        }
        target = voidTarget = true;
        placeBox = boxOf(placeCell); removeBox = -1;
        return;
    }
    const float e = 1e-5f;
    for (int a = 0; a < 3; ++a) {
        float pin = h.pos[a] - h.normal[a] * e - 1.0f, pout = h.pos[a] + h.normal[a] * e - 1.0f;
        removeCell[a] = (int32_t)std::floor(pin * n);
        placeCell[a] = (int32_t)std::floor(pout * n);
    }
    if (placeCell[0] == removeCell[0] && placeCell[1] == removeCell[1] && placeCell[2] == removeCell[2])
        for (int a = 0; a < 3; ++a) placeCell[a] += h.normal[a];
    placeBox = boxOf(placeCell);
    removeBox = boxOf(removeCell);
}

void BuildTool::begin(Action a) {
    if (a == NONE || !target || (a == REMOVE && voidTarget)) { drag = NONE; return; }
    dragBox = a == PLACE ? placeBox : removeBox;
    if (dragBox < 0) { drag = NONE; return; }          // outside the allowed boxes
    drag = a;
    dragLevel = level();
    const int32_t* c = a == PLACE ? placeCell : removeCell;
    for (int i = 0; i < 3; ++i) anchor[i] = c[i];
}

CellBox BuildTool::region() const {
    CellBox b;
    b.level = level();
    const int32_t* cur = drag == REMOVE ? removeCell : placeCell;
    for (int i = 0; i < 3; ++i) {
        int32_t a = drag != NONE ? anchor[i] : cur[i];
        int32_t c = cur[i];
        c = std::clamp(c, a - (cfg.maxExtent - 1), a + (cfg.maxExtent - 1));   // bounded size
        b.lo[i] = std::min(a, c);
        b.hi[i] = std::max(a, c);
    }
    if (allowed && drag != NONE && dragBox >= 0 && dragBox < (int)allowed->size()) {   // stay in the box
        int32_t lo[3], hi[3];
        if (cellsInside((*allowed)[dragBox], b.level, lo, hi))
            for (int i = 0; i < 3; ++i) { b.lo[i] = std::max(b.lo[i], lo[i]); b.hi[i] = std::min(b.hi[i], hi[i]); }
    }
    return b;
}

bool BuildTool::end(Action a, Edit& out) {
    if (drag == NONE || a != drag || dragLevel != level()) { drag = NONE; return false; }
    out = Edit{};
    out.op = a == PLACE ? Edit::PLACE : Edit::REMOVE;
    out.box = region();
    out.voxel = voxel();
    out.colorNoise = cfg.colorNoise;
    drag = NONE;
    return true;
}

void BuildTool::cellBoxOverlay(const CellBox& b, float inflate, const float rgb[3], float alpha, int style,
                               Overlay& o) const {
    const float s = 1.0f / (float)(1 << b.level);
    for (int a = 0; a < 3; ++a) {
        o.lo[a] = 1.0f + b.lo[a] * s - inflate * s;
        o.hi[a] = 1.0f + (b.hi[a] + 1) * s + inflate * s;
    }
    o.color[0] = rgb[0]; o.color[1] = rgb[1]; o.color[2] = rgb[2]; o.color[3] = alpha;
    o.style = style;
}

int BuildTool::ghosts(Overlay* out, int max) const {
    int n = 0;
    if (!target || max <= 0) return 0;
    float col[3];
    unpack(colors[sel], col);
    const float white[3] = {1.0f, 1.0f, 1.0f}, red[3] = {1.0f, 0.25f, 0.2f};
    if (drag == PLACE) {
        cellBoxOverlay(region(), 0.0f, col, 0.45f, OVERLAY_FILL_HOST, out[n++]);
    } else if (drag == REMOVE) {
        cellBoxOverlay(region(), 0.02f, red, 0.35f, OVERLAY_FILL_HOST, out[n++]);
    } else {
        // Hover: outline the block that a left click removes, and a faint
        // ghost of the block that a right click places.
        CellBox rb; rb.level = level();
        CellBox pb; pb.level = level();
        for (int a = 0; a < 3; ++a) { rb.lo[a] = rb.hi[a] = removeCell[a]; pb.lo[a] = pb.hi[a] = placeCell[a]; }
        if (voidTarget) {   // in the void: only the block a right click places, outlined
            if (placeBox < 0) return 0;
            cellBoxOverlay(pb, 0.0f, col, 0.40f, OVERLAY_FILL_HOST, out[n++]);
            if (n < max) cellBoxOverlay(pb, 0.0f, white, 0.0f, OVERLAY_LINES_HOST, out[n++]);
            return n;
        }
        if (removeBox >= 0) cellBoxOverlay(rb, 0.02f, white, 0.0f, OVERLAY_LINES_HOST, out[n++]);
        if (n < max && placeBox >= 0) cellBoxOverlay(pb, 0.0f, col, 0.30f, OVERLAY_FILL_HOST, out[n++]);
    }
    return n;
}
