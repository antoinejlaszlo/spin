#pragma once
#include <vector>
#include "octree.hpp"

// Player physics: a sphere colliding with the solid voxels of the octree
// (octree space, the scene spans [1,2]^3). The camera sits rigidly at the
// sphere's centre.
//
// "Support up" S is the direction the player stands along: world up normally,
// or the player's own (axis-aligned) up while gripping a grip voxel surface.
// Gravity pulls along -S: world gravity normally; while gripping, world
// gravity is off and the player is held against the grip surface instead.
struct Vec3 { float x = 0, y = 0, z = 0; };
class Portals;

struct PlayerInput {
    float forward = 0, strafe = 0;   // -1..1
    bool jump = false, sprint = false;

    // View directions (physical, world space) and the body's up axis. Walking
    // moves along the view projected onto the surface the player stands on.
    bool hasView = false;
    Vec3 viewF{1, 0, 0}, viewR{0, 0, 1}, viewU{0, 1, 0};
    Vec3 bodyUp{0, 1, 0};
    float yaw = 0;                   // used instead of the view when !hasView (upright only)
};

struct PlayerConfig {
    float radius = 0.020f;           // sphere radius (~2.5 default voxels); camera at the centre.
                                     // Ledges lower than the radius are rolled over automatically.
    float walkSpeed = 0.13f;         // units / s
    float sprintMul = 1.8f;
    float gravity = 0.60f;           // units / s^2 (also the pull toward a gripped surface)
    float jumpSpeed = 0.20f;         // -> ~0.033 units (~4 voxels) jump
    float groundAccel = 12.0f;       // how fast velocity reaches the target on a surface (1/s)
                                     // (in the air nothing but gravity changes the velocity)
    float maxFall = 1.5f;
    float snapDown = 0.008f;         // stay on the surface over drops up to ~1 voxel
    // A grip voxel only grips when it touches the very bottom of the sphere
    // (relative to the body's up): the contact direction must be within
    // ~3.6 degrees of straight down the body (cos 3.6 deg = 0.998). Otherwise
    // it is an ordinary surface (floor or wall) for the player.
    float gripAlign = 0.998f;
};

class VoxelWorld {
public:
    explicit VoxelWorld(const Octree& o) : oct(o) {}
    struct Box { Vec3 lo, hi; bool grip; };
    // All solid leaf boxes overlapping [lo, hi].
    void query(const Vec3& lo, const Vec3& hi, std::vector<Box>& out) const;
private:
    const Octree& oct;
};

class Player {
public:
    PlayerConfig cfg;
    Vec3 pos;            // sphere centre (= camera position)
    Vec3 vel;
    bool grounded = false;           // standing on something along S
    bool jumped = false;             // airborne because of a jump (cleared on landing)
    bool gripping = false;           // stuck to a grip surface (world gravity off)
    // Gripping any surface, not only grip voxels: set when a portal delivers
    // you, while supported, onto a door standing on a wall or ceiling. Ends
    // with the grip (jump, walking off the surface).
    bool gripAny = false;
    Vec3 support{0, 1, 0};           // S: world up, or the gripped surface's normal
    Portals* portals = nullptr;      // optional: walk through the open door (swaps which is open)
    int teleports = 0;               // number of portal crossings so far
    int respawns = 0;                // number of respawns so far (incl. falling off the world)
    // Rotation of all portal crossings since the last takeRotation() (row-major 3x3),
    // for turning the view the same way.
    bool takeRotation(float m[9]);

    Player(const VoxelWorld& w, Vec3 spawn) : world(w), spawnPos(spawn) { respawn(); }
    void respawn();
    void update(float dt, const PlayerInput& in);   // any dt; substeps internally
    Vec3 eye() const { return pos; }
    void setSpawn(Vec3 s) { spawnPos = s; }
    // Standing on normal (non-grip) ground while the body is not upright:
    // the view should be turned upright (keeping the look direction).
    bool needsUpright() const { return grounded && !gripping && bodyUp.y < 0.99f; }

    // Size: k times the configuration the player had at scale 1 (radius,
    // speeds, gravity, jump: everything that is a length scales with k, so
    // moving feels the same at any size). The bottom of the sphere stays where
    // it was. Fails (no change) when there is no room to grow.
    // force: change even without room (the caller moves the player next).
    bool setScale(float k, bool force = false);
    float scale() const { return curScale; }

private:
    const VoxelWorld& world;
    Vec3 spawnPos;
    float curScale = 1.0f;
    PlayerConfig unit;               // cfg at scale 1 (captured when leaving scale 1)
    Vec3 bodyUp{0, 1, 0};
    float coyote = 0;                // seconds left in which a jump is still allowed
    std::vector<VoxelWorld::Box> boxes;
    float pendingRot[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    bool hasPendingRot = false;

    void step(float dt, PlayerInput& in);   // may rotate `in` (portal crossing)
    // Push the sphere out of voxels. support: something holds us along S;
    // gripSupport: that something is a grip voxel. May start gripping.
    void resolve(bool& support, bool& gripSupport);
    bool overlaps();
};
