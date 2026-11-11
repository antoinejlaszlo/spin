#pragma once
// Models and the object tool.
//
// A Model is the content of a dream cage: its voxels, relative to the cage.
// The object tool holds one ("in hand") in the level editor and places copies
// of it, like a block: on the face you aim at (its side against the face,
// centred on the aimed point), or in the air ahead when nothing is in reach.
// The ghost (the model itself, drawn see-through by the renderer) only shows
// where the copy fits: no overlap with voxels, doors, the start box or you.
// Copies scale with the player (2x bigger player: 2x bigger copy).
//
// A Model can also be a REFERENCE: one cage voxel (VOXEL_INSTANCE) standing
// for a cage's content. It is placed as a single node of the octree (its cube
// on the grid of its own size) and keeps showing whatever the cage holds.
// In the dream, models go into cages only, and never into a cage they contain
// (a cage can't hold itself, even through nested cage voxels).
#include <string>
#include <vector>
#include "renderer.hpp"
#include "world.hpp"

class Dream;

struct Model {
    std::string name;
    bool reference = false;                  // one cage voxel pointing at cage `cage`
    int cage = -1;
    // Voxels: level = their octree level in the dream; x, y, z relative to the
    // cage's corner at that level (cage = a cube at level cageLevel).
    std::vector<VoxelStore::Leaf> leaves;
    int cageLevel = 4;
    int minLevel = 0, maxLevel = 0;          // largest / smallest voxel
    int32_t lo[3] = {0, 0, 0}, hi[3] = {0, 0, 0};   // tight bounds, cells at maxLevel, hi exclusive
    Octree ghost;                            // the voxels in an octree whose root cube is the cage
    bool empty() const { return leaves.empty(); }
};

// The voxels inside `interior` (a cage of `world`, a cube at level cageLevel);
// cage voxels in it stay cage voxels. resolver: for the ghost.
Model extractModel(const World& world, const CellBox& interior, int cageLevel, const InstanceResolver* resolver);
// A reference to cage `cage` of the dream.
Model makeReference(const Dream& dream, int cage);
// (Re)builds model.ghost from its voxels (cage voxels show their current content).
void buildGhost(Model& m, const InstanceResolver* resolver);

class ObjectTool {
public:
    float reach = 0.35f;          // scaled with the player by the caller
    float voidDistance = 0.06f;

    void hold(Model m);
    bool holding() const { return !model.empty(); }
    // The dream changed: show the held cage voxels' current content.
    void refreshGhost(const InstanceResolver* r) { if (holding()) buildGhost(model, r); }
    const Model& held() const { return model; }
    void drop() { model = Model{}; valid = false; }

    // scaleShift k: the copy is 2^k times the model's size. keepOut: the
    // player's sphere (xyz r) or null. intoDream: placing in the dream (inside
    // one cage, no cycles), null in a level.
    void aim(const PickHit& hit, const float eye[3], const float dir[3], const World& world, int scaleShift,
             const float* keepOut, const Dream* intoDream = nullptr);
    bool canPlace() const { return valid; }
    int place(World& world);                 // right click: the number of voxels placed (0: nothing)
    void fillGhost(Params& p) const;         // see-through preview, when it fits
    const char* why() const { return reason; }   // why it can't be placed here

private:
    Model model;
    bool valid = false;
    int k = 0;                               // scale shift used for the target
    int32_t origin[3] = {0, 0, 0};           // cage corner, in cells at level placeLevel()
    const char* reason = "";
    int placeLevel() const { return model.minLevel - k; }
    const char* fits(const World& world, const float* keepOut, const Dream* intoDream) const;   // null: fits
    // Cache: the last checked placement.
    int32_t lastOrigin[3] = {0, 0, 0};
    int lastK = 1 << 20;
    const char* lastWhy = "";
    const Dream* lastDream = nullptr;
    float lastKeep[4] = {0, 0, 0, 0};
    uint64_t lastVersion = ~0ull;
};
