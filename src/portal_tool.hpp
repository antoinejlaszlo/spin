#pragma once
// Portal tool: places and removes the two portal doors, with ghosts.
//
//  - Right button: press on a voxel face where the door's bottom corner goes,
//    drag to the opposite corner. The door stands on that face (its up is the
//    face's normal: a floor, a wall or a ceiling), in the plane that faces you
//    most (its entrance side faces you). The first door can be any size up to
//    cfg.maxExtent; the second one always has the first one's size (both
//    doors must match), so its ghost shows it at that size.
//  - Left button on a door: removes it.
// Like BuildTool, it has no window code.
#include "build_tool.hpp"
#include "portals.hpp"

class PortalTool {
public:
    struct Config {
        int maxExtent = 16;      // max door width / height in cells
        float reach = 0.35f;
    };
    Config cfg;

    // aim: crosshair pick + the view ray (needed to drag in the door's plane).
    // level: block size to use when no door exists yet.
    void aim(const PickHit& hit, const float eye[3], const float dir[3], const Portals& portals, int level);
    void begin(BuildTool::Action a, const Portals& portals);
    // On release: creates / removes doors. Returns true if the world changed.
    bool end(BuildTool::Action a, Portals& portals, World& world, const float* keepOut);
    void cancel() { drag = BuildTool::NONE; }
    BuildTool::Action dragging() const { return drag; }

    int ghosts(const Portals& portals, Overlay* out, int max) const;

    // The door that would be created now (valid if hasCandidate()).
    bool hasCandidate() const { return candidateOk; }
    const CellBox& candidate() const { return cand; }
    int candidateUpAxis() const { return upAxis; }
    int candidateUpSign() const { return upSign; }
    int candidateAxis() const { return axis; }
    int candidateSign() const { return sign; }

private:
    BuildTool::Action drag = BuildTool::NONE;
    bool target = false, candidateOk = false;
    int hoverDoor = -1;                 // door under the crosshair (for removal)
    int level = 7;
    int32_t place[3] = {0, 0, 0};       // cell just outside the aimed face
    int upAxis = 1, upSign = 1;         // door's up: the normal of the face it stands on
    int axis = 0, sign = 1;             // door plane normal (entrance side), perpendicular to up
    int32_t anchor[3] = {0, 0, 0};
    int32_t current[3] = {0, 0, 0};
    CellBox cand;

    void updateCandidate(const Portals& portals);
};
