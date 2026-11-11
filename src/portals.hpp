#pragma once
// Portals: a pair of doors. Each door is a rectangle of black voxels (a
// locked wall in the World) with an analytic doorway pasted on its entrance
// face. At most one door is open at a time: looking or walking into the open
// door's entrance continues out of the other door's entrance side. The moment
// the camera (the player's sphere centre) goes through, the doors swap: the
// one you came through closes and the one you came out of opens.
//
// A door stands on a voxel face: its "up" is that face's normal (floor,
// wall or ceiling) and its entrance faces one of the 4 axes perpendicular to
// it. All door axes are world axes, so going through maps world axes onto
// world axes (one of the 24 axis-aligned rotations): air rotations, grip and
// gravity keep working.
#include <cstdint>
#include "physics.hpp"
#include "renderer.hpp"
#include "world.hpp"

struct PortalDoor {
    bool exists = false;
    CellBox wall;            // the black voxels (one cell thick)
    int upAxis = 1;          // the door's up (normal of the face it stands on): axis 0/1/2...
    int upSign = 1;          // ...and sign
    int normalAxis = 0;      // entrance side: axis (perpendicular to up)...
    int normalSign = 1;      // ...and sign
    PortalFrame frame;       // doorway on the entrance face
    int lockId = 0;
};

class Portals {
public:
    static constexpr uint32_t kWallColor = 0xFF0A0A0Au;   // near-black

    PortalDoor door[2];
    int open = 0;            // index of the open door (when both exist)

    bool linked() const { return door[0].exists && door[1].exists; }
    int count() const { return (int)door[0].exists + (int)door[1].exists; }
    int freeSlot() const { return !door[0].exists ? 0 : !door[1].exists ? 1 : -1; }

    // Doorway frame for a wall box standing along up, entrance along normal.
    static PortalFrame frameFor(const CellBox& wall, int upAxis, int upSign, int normalAxis, int normalSign);

    // Creates a door: writes the black wall into the world and locks it.
    // Fails (false) if there is no free slot, the wall overlaps a locked
    // region (the other door), or it would intersect the keep-out sphere.
    // The second door must have the same size as the first.
    bool create(World& world, const CellBox& wall, int upAxis, int upSign, int normalAxis, int normalSign,
                const float* keepOut /* xyz r, or null */);
    void remove(World& world, int index);          // clears the wall and unlocks it
    int doorAt(const float pos[3]) const;          // door whose wall contains pos (-1: none)

    // Rendering: the open doorway and where it leads.
    void fillParams(Params& p) const;

    // Mapping from the open door to the other one.
    Vec3 mapPoint(Vec3 p) const;
    Vec3 mapDir(Vec3 d) const;
    void rotation(float m[9]) const;               // mapDir as a row-major 3x3 matrix

    // Physics: may the player at `centre` ignore this box? True for boxes
    // entirely behind the open doorway (its wall and what is behind it) while
    // the centre is in front of the doorway and inside its outline: you can
    // step into the open door instead of bumping into its wall.
    bool ignoreBox(const VoxelWorld::Box& b, const Vec3& centre) const;
    // Did the segment a -> b enter the open doorway from its entrance side?
    bool crosses(const Vec3& a, const Vec3& b) const;
    void swapOpen() { open = 1 - open; }
};
