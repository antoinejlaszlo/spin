#pragma once
// Levels. A level is everything a playable map owns: its own editable world
// (octree), its portals, and the player's start position. The editor edits
// the current level; play mode plays it. A LevelSet holds the levels of a
// game (only one for now, but nothing assumes that).
#include <memory>
#include <string>
#include <vector>
#include "portals.hpp"
#include "world.hpp"

// Where the player starts: a box of the player's size standing on a floor,
// with a facing direction. Its cells are locked in the world, so no voxel or
// door can be placed inside it. Visible in the editor only.
struct StartPoint {
    bool set = false;
    CellBox box;             // the marker (and keep-clear region)
    float yaw = 0;           // facing (radians, PlayerView::reset convention: 0 = +x, pi/2 = +z)
    int lockId = 0;
    // Sphere centre for a player of `radius`, resting on the floor under the box.
    Vec3 eye(float radius) const;
};

class Level {
public:
    Level(std::string name, const Octree& initial) : name(std::move(name)), world(initial) {}
    std::string name;
    World world;
    Portals portals;
    StartPoint start;

    // Can the start box go there? (inside the world, no solid voxel, no
    // locked region such as a door — its own current box excepted)
    bool startFree(const CellBox& box) const;
    bool setStart(const CellBox& box, float yaw);   // false if !startFree
    void clearStart();
    bool playable() const { return start.set; }     // play mode needs a start position
};

class Dream;

class LevelSet {
public:
    int baseLevel = 7;       // octree level of the default block size (the scene's voxels)
    std::shared_ptr<Dream> dream;   // the dream (models in cages), shared by all levels; see dream.hpp
    Level& add(const std::string& name, const Octree& initial) {
        levels.push_back(std::make_unique<Level>(name, initial));
        return *levels.back();
    }
    int count() const { return (int)levels.size(); }
    Level& at(int i) { return *levels[i]; }
    Level& current() { return *levels[cur]; }
    int currentIndex() const { return cur; }
    void select(int i) { if (i >= 0 && i < count()) cur = i; }
    void clear() { levels.clear(); cur = 0; dream.reset(); }
private:
    std::vector<std::unique_ptr<Level>> levels;   // stable addresses (renderer/physics keep references)
    int cur = 0;
};
