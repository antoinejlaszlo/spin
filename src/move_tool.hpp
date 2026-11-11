#pragma once
// Move tool (editor): select a built object and slide it along the axes.
//
//  - Left click on a built block selects the whole object it belongs to: all
//    built voxels connected to it face to face (scene voxels and portal doors
//    are not part of objects and never move). The object is shown tinted, with
//    a gizmo: three arrows from its centre, red x, green y, blue z, drawn on
//    top of everything, and its box outlined (cage voxels share their content
//    with every other copy, so they are not tinted).
//  - Left drag on an arrow moves the object along that axis, following the
//    mouse, in steps of its largest voxel. It stops at the first step that
//    would overlap anything (other voxels, doors, the start box, the player,
//    the world's edge): objects never overlap.
//  - Right click (or a click on nothing / on the scene) deselects.
// Moves are ordinary Edits (REMOVE + PLACE), so they are in World::history().
// No window code (like the other tools).
#include <string>
#include <vector>
#include "renderer.hpp"
#include "world.hpp"

class MoveTool {
public:
    struct Config {
        float reach = 0.35f;
        size_t maxLeaves = 1 << 16;      // largest object that can be selected
        float arrowMargin = 0.025f;      // arrows reach this far past the object
        float arrowWidth = 0.0025f;      // shaft thickness
        float pickWidth = 0.006f;        // easier to grab than to see
        float knob = 0.008f;             // arrow tip cube
        float mouseSens = 0.0022f;       // radians per pixel (same as mouse look)
    };
    Config cfg;

    void aim(const PickHit& hit, const float eye[3], const float dir[3]);
    // Left button pressed: grab the hovered arrow, or select what the reticle
    // points at. Returns true if the selection changed (World needs a commit).
    bool press(World& world);
    void release() { axis = -1; accum = 0; }
    bool deselect(World& world);
    // Mouse motion (pixels) while dragging an arrow; right/up: the view's.
    // Returns the number of steps moved (World needs a commit if nonzero).
    int drag(float dx, float dy, const float eye[3], const float right[3], const float up[3],
             World& world, const float* keepOut);
    // Moves the selection along an axis by up to `steps` (signed) steps,
    // stopping before the first overlap. Returns the steps actually moved.
    int moveBy(World& world, int ax, int steps, const float* keepOut /* xyz r, or null */);

    bool dragging() const { return axis >= 0; }
    int hoveredAxis() const { return hover; }
    bool hasSelection() const { return !sel.empty(); }
    size_t selectedCount() const { return sel.size(); }
    float step() const { return 1.0f / (float)(1 << coarse); }   // move granularity (octree units)
    void bounds(float lo[3], float hi[3]) const;                // selection box, octree space
    const std::string& message() const { return msg; }          // why the last press did what it did

    int ghosts(Overlay* out, int max) const;    // the gizmo

private:
    std::vector<VoxelStore::Leaf> sel;           // the object (voxels without VOXEL_SELECTED)
    int coarse = 0;                              // level of its largest voxel
    bool target = false;
    float aimPos[3] = {0, 0, 0};                 // just inside the aimed voxel
    int hover = -1, axis = -1;
    float accum = 0;                             // dragged distance not yet moved (octree units)
    std::string msg;

    bool select(World& world, const float p[3]);
    bool blocked(const World& world, int ax, int cells, const float* keepOut) const;
    void arrow(int ax, float pick, float lo[3], float hi[3], float knobLo[3], float knobHi[3]) const;
};
