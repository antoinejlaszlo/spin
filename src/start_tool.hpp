#pragma once
// Start tool (editor): sets the level's start position.
//  - Right click on a floor (an upward face): puts the start box there,
//    centred on the aimed cell, facing the way you look (snapped to 90 deg).
//    Only one start exists: placing again moves it. Refused (red ghost) where
//    the box would overlap voxels or a door.
//  - Left click on the start box: removes it.
// No window code (like BuildTool / PortalTool).
#include "build_tool.hpp"
#include "level.hpp"

class StartTool {
public:
    float reach = 0.35f;

    // level: octree level of the box cells; radius: the player's.
    void aim(const PickHit& hit, const float eye[3], const float dir[3], const Level& lvl, int level, float radius);
    bool place(Level& lvl);      // right click: true if the start changed
    bool remove(Level& lvl);     // left click: true if removed
    int ghosts(const Level& lvl, Overlay* out, int max) const;   // the candidate box, or the start box to remove
    static int marker(const StartPoint& s, Overlay* out, int max);   // the existing start box (editor only)

    bool hasCandidate() const { return candidate; }
    bool candidateValid() const { return valid; }
    const CellBox& candidateBox() const { return cand; }
    float candidateYaw() const { return yaw; }
    bool hoveringStart() const { return hover; }

private:
    bool candidate = false, valid = false, hover = false;
    CellBox cand;
    float yaw = 0;
};
