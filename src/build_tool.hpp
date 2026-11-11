#pragma once
// Building: what block is selected, which cells the crosshair targets, mouse
// drags into rectangles, the ghost overlays that preview the result, and the
// Edit commands it produces. No window/GL code here (see build_ui.* and
// viewer.cpp for input and drawing), so it can be tested and reused (e.g. by
// a future level editor or scripted tools).
#include <algorithm>
#include <cstdint>
#include <vector>
#include "renderer.hpp"
#include "world.hpp"

class BuildTool {
public:
    enum Action { NONE, PLACE, REMOVE };

    struct Config {
        int maxExtent = 32;          // max cells per axis in one drag (bounded edits)
        float reach = 0.35f;         // pick distance (octree units; ~45 default blocks)
        int sizeSteps = 5;           // block sizes: 1, 1/2, 1/4, 1/8, 1/16 of the default
        float colorNoise = 0.03f;    // +-3% brightness per placed cell, so block edges read
        float voidDistance = 0.06f;  // aiming at nothing: the block goes this far from the eye (~8 default blocks)
    };
    Config cfg;

    explicit BuildTool(int baseLevel);

    // Which tool the mouse drives (the other tools live in their own files;
    // the selection here, block size included, is shared). Which tools exist
    // depends on where you are (levels / the dream, object in hand).
    // TOOL_NONE: no tool (clicks do nothing, no ghost); always available.
    enum Tool { TOOL_NONE, TOOL_BLOCKS, TOOL_PORTAL, TOOL_START, TOOL_MOVE, TOOL_CAGE, TOOL_REFERENCE, TOOL_OBJECT, TOOL_COUNT };
    Tool tool() const { return current; }
    void setTool(Tool t) { if (available(t)) { cancel(); current = t; } }
    void setAvailable(unsigned mask);              // bit i = tool i (blocks and none always); falls back to blocks
    bool available(Tool t) const { return (avail >> t) & 1u; }
    void nextTool();                               // the next available tool (V)

    // --- selection -------------------------------------------------------
    const std::vector<uint32_t>& palette() const { return colors; }
    int selected() const { return sel; }
    void select(int i);
    bool grip() const { return gripOn; }
    void setGrip(bool g) { gripOn = g; }
    int sizeStep() const { return step; }          // 0 = default size, k = 1/2^k
    void setSizeStep(int s);
    // Octree level of the selected block size, relative to the player's size:
    // scaleShift k = the player is 2^k times the normal size (k < 0: smaller).
    int level() const { return std::clamp(base + step - shift, 1, VoxelStore::kMaxLevel); }
    void setScaleShift(int k) { if (k != shift) cancel(); shift = k; }
    int scaleShift() const { return shift; }
    // Building only inside these boxes (null: anywhere). Placing / removing
    // needs the targeted cell inside one of them, and a drag stays in that box.
    void setAllowed(const std::vector<CellBox>* boxes) { if (boxes != allowed) cancel(); allowed = boxes; }
    float cellSize() const { return 1.0f / (float)(1 << level()); }
    Voxel voxel() const;

    // --- per frame -------------------------------------------------------
    // Where the crosshair points (pickVoxel result; hit=false for nothing in
    // reach). With the eye and view direction, aiming at nothing targets the
    // cell at cfg.voidDistance along the view ("building in the void"): you
    // can place there, not remove.
    void aim(const PickHit& hit, const float* eye = nullptr, const float* dir = nullptr);
    bool hasTarget() const { return target; }
    bool inVoid() const { return voidTarget; }

    // --- mouse -----------------------------------------------------------
    void begin(Action a);                          // button pressed: anchor the drag
    bool end(Action a, Edit& out);                 // button released: the edit to apply
    void cancel() { drag = NONE; }
    Action dragging() const { return drag; }
    CellBox region() const;                        // what the current drag (or click) covers

    // --- preview -----------------------------------------------------------
    int ghosts(Overlay* out, int max) const;       // overlay boxes for the renderer

private:
    Tool current = TOOL_BLOCKS;
    unsigned avail = (1u << TOOL_NONE) | (1u << TOOL_BLOCKS) | (1u << TOOL_PORTAL) | (1u << TOOL_START) | (1u << TOOL_MOVE);
    int base, step = 0, sel = 0, shift = 0;
    const std::vector<CellBox>* allowed = nullptr;
    int placeBox = -1, removeBox = -1, dragBox = -1;   // allowed box of each target / the drag
    bool gripOn = false;
    std::vector<uint32_t> colors;
    bool target = false, voidTarget = false;
    int32_t placeCell[3] = {0, 0, 0}, removeCell[3] = {0, 0, 0};
    Action drag = NONE;
    int dragLevel = 0;
    int32_t anchor[3] = {0, 0, 0};

    void cellBoxOverlay(const CellBox& b, float inflate, const float rgb[3], float alpha, int style, Overlay& o) const;
    int boxOf(const int32_t cell[3]) const;        // allowed box containing the cell at level(), -1 none
};

// The cells of `level` that lie inside box b (any level). False if none.
bool cellsInside(const CellBox& b, int level, int32_t lo[3], int32_t hi[3]);
