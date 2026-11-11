#include "portal_tool.hpp"
#include <algorithm>
#include <cmath>

namespace {

const float kDoorColor[2][3] = {{0.25f, 0.55f, 1.0f}, {1.0f, 0.55f, 0.15f}};   // like Portal: blue, orange

void boxOverlay(const CellBox& b, float inflate, const float rgb[3], float alpha, int style, Overlay& o) {
    const float s = 1.0f / (float)(1 << b.level);
    for (int a = 0; a < 3; ++a) {
        o.lo[a] = 1.0f + b.lo[a] * s - inflate * s;
        o.hi[a] = 1.0f + (b.hi[a] + 1) * s + inflate * s;
    }
    o.color[0] = rgb[0]; o.color[1] = rgb[1]; o.color[2] = rgb[2]; o.color[3] = alpha;
    o.style = style;
}

} // namespace

void PortalTool::aim(const PickHit& h, const float eye[3], const float dir[3], const Portals& portals, int defaultLevel) {
    // Doors all use one grid: the existing door's, else the selected block size.
    level = defaultLevel;
    for (const auto& d : portals.door) if (d.exists) level = d.wall.level;
    const float n = (float)(1 << level);

    target = h.hit && h.t <= cfg.reach;
    hoverDoor = -1;
    if (target) {
        float inside[3];
        for (int a = 0; a < 3; ++a) inside[a] = h.pos[a] - h.normal[a] * 1e-5f;
        hoverDoor = portals.doorAt(inside);
    }

    if (drag == BuildTool::PLACE && portals.count() == 0) {
        // Dragging the first door: follow the view ray in the door's plane.
        const float plane = 1.0f + (anchor[axis] + 0.5f) / n;
        if (std::fabs(dir[axis]) > 1e-6f) {
            float t = (plane - eye[axis]) / dir[axis];
            if (t > 0.0f && t < 4.0f * cfg.reach)
                for (int a = 0; a < 3; ++a)
                    if (a != axis) current[a] = (int32_t)std::floor((eye[a] + dir[a] * t - 1.0f) * n);
        }
        current[axis] = anchor[axis];
    } else if (target) {
        int32_t in[3];
        for (int a = 0; a < 3; ++a) {
            in[a] = (int32_t)std::floor((h.pos[a] - h.normal[a] * 1e-5f - 1.0f) * n);
            place[a] = (int32_t)std::floor((h.pos[a] + h.normal[a] * 1e-5f - 1.0f) * n);
        }
        if (in[0] == place[0] && in[1] == place[1] && in[2] == place[2])
            for (int a = 0; a < 3; ++a) place[a] += h.normal[a];
        if (drag == BuildTool::NONE) {
            // The door stands on the aimed face (up = its normal) and faces
            // you: of the two axes perpendicular to up, the one you look along
            // most is the door's normal, entrance toward you.
            for (int a = 0; a < 3; ++a) if (h.normal[a]) { upAxis = a; upSign = h.normal[a]; }
            int a1 = (upAxis + 1) % 3, a2 = (upAxis + 2) % 3;
            axis = std::fabs(dir[a1]) >= std::fabs(dir[a2]) ? a1 : a2;
            sign = dir[axis] > 0.0f ? -1 : 1;
        }
        for (int a = 0; a < 3; ++a) current[a] = place[a];
    }
    updateCandidate(portals);
}

void PortalTool::updateCandidate(const Portals& portals) {
    candidateOk = false;
    if (portals.freeSlot() < 0) return;
    const int ua = upAxis, ra = 3 - upAxis - axis;     // height axis, width axis
    cand = CellBox{};
    cand.level = level;
    // Height runs from the bottom cell (on the face) away from the face, along up.
    auto heightRange = [&](int32_t bottom, int32_t cells) {
        if (upSign > 0) { cand.lo[ua] = bottom; cand.hi[ua] = bottom + cells - 1; }
        else            { cand.hi[ua] = bottom; cand.lo[ua] = bottom - cells + 1; }
    };
    if (portals.count() == 1) {
        // Second door: same size as the first, bottom-centred on the aimed cell.
        if (!target) return;
        const PortalDoor& other = portals.door[portals.door[0].exists ? 0 : 1];
        const float s = 1.0f / (float)(1 << level);
        int w = (int)std::lround(2.0f * other.frame.halfW / s), hgt = (int)std::lround(2.0f * other.frame.halfH / s);
        cand.lo[axis] = cand.hi[axis] = current[axis];
        cand.lo[ra] = current[ra] - (w - 1) / 2;
        cand.hi[ra] = cand.lo[ra] + w - 1;
        heightRange(current[ua], hgt);
    } else {
        // First door: the rectangle between the press cell and the dragged-to
        // cell, never going into the face it stands on.
        if (drag != BuildTool::PLACE && !target) return;
        const int32_t* a = drag == BuildTool::PLACE ? anchor : current;
        int32_t cw = std::clamp(current[ra], a[ra] - (cfg.maxExtent - 1), a[ra] + (cfg.maxExtent - 1));
        cand.lo[ra] = std::min(a[ra], cw);
        cand.hi[ra] = std::max(a[ra], cw);
        int32_t up = (current[ua] - a[ua]) * upSign;   // cells dragged away from the face
        heightRange(a[ua], std::clamp(up, 0, cfg.maxExtent - 1) + 1);
        cand.lo[axis] = cand.hi[axis] = a[axis];
    }
    const int32_t n = 1 << level;
    for (int i = 0; i < 3; ++i) if (cand.lo[i] < 0 || cand.hi[i] >= n) return;   // inside the world
    candidateOk = true;
}

void PortalTool::begin(BuildTool::Action a, const Portals& portals) {
    drag = BuildTool::NONE;
    if (a == BuildTool::PLACE && candidateOk && portals.freeSlot() >= 0) {
        drag = a;
        for (int i = 0; i < 3; ++i) anchor[i] = current[i];
    } else if (a == BuildTool::REMOVE && hoverDoor >= 0) {
        drag = a;
    }
}

bool PortalTool::end(BuildTool::Action a, Portals& portals, World& world, const float* keepOut) {
    if (a != drag) { drag = BuildTool::NONE; return false; }
    bool changed = false;
    if (a == BuildTool::PLACE) {
        updateCandidate(portals);
        if (candidateOk) changed = portals.create(world, cand, upAxis, upSign, axis, sign, keepOut);
    } else if (a == BuildTool::REMOVE && hoverDoor >= 0) {
        portals.remove(world, hoverDoor);
        changed = true;
    }
    drag = BuildTool::NONE;
    return changed;
}

int PortalTool::ghosts(const Portals& portals, Overlay* out, int max) const {
    int k = 0;
    const float red[3] = {1.0f, 0.25f, 0.2f}, black[3] = {0.02f, 0.02f, 0.03f};
    // Existing doors: outlined in their colour.
    for (int i = 0; i < 2 && k < max; ++i)
        if (portals.door[i].exists) boxOverlay(portals.door[i].wall, 0.03f, kDoorColor[i], 0.0f, OVERLAY_LINES_HOST, out[k++]);
    if (drag != BuildTool::PLACE && hoverDoor >= 0 && k < max) {          // left click removes it
        boxOverlay(portals.door[hoverDoor].wall, 0.05f, red, 0.2f, OVERLAY_FILL_HOST, out[k++]);
        return k;
    }
    if (candidateOk && k + 1 < max) {
        const int slot = portals.freeSlot();
        boxOverlay(cand, 0.0f, black, 0.55f, OVERLAY_FILL_HOST, out[k++]);
        // The entrance side: a thin coloured plate on the face that leads through.
        CellBox face = cand;
        Overlay& o = out[k++];
        boxOverlay(face, 0.0f, kDoorColor[slot], 0.55f, OVERLAY_FILL_HOST, o);
        const float s = 1.0f / (float)(1 << cand.level);
        float faceAt = sign > 0 ? o.hi[axis] : o.lo[axis];
        o.lo[axis] = faceAt - (sign > 0 ? 0.0f : 0.15f * s);
        o.hi[axis] = faceAt + (sign > 0 ? 0.15f * s : 0.0f);
    }
    return k;
}
