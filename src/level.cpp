#include "level.hpp"
#include "physics.hpp"

Vec3 StartPoint::eye(float radius) const {
    const float s = 1.0f / (float)(1 << box.level);
    return {1.0f + (box.lo[0] + box.hi[0] + 1) * 0.5f * s,
            1.0f + box.lo[1] * s + radius + 1e-4f,
            1.0f + (box.lo[2] + box.hi[2] + 1) * 0.5f * s};
}

bool Level::startFree(const CellBox& box) const {
    const int32_t n = 1 << box.level;
    for (int a = 0; a < 3; ++a) if (box.lo[a] < 0 || box.hi[a] >= n || box.lo[a] > box.hi[a]) return false;
    if (world.isLocked(box, start.set ? start.lockId : 0)) return false;
    const float s = 1.0f / (float)(1 << box.level), e = 1e-4f;
    Vec3 lo{1.0f + box.lo[0] * s + e, 1.0f + box.lo[1] * s + e, 1.0f + box.lo[2] * s + e};
    Vec3 hi{1.0f + (box.hi[0] + 1) * s - e, 1.0f + (box.hi[1] + 1) * s - e, 1.0f + (box.hi[2] + 1) * s - e};
    std::vector<VoxelWorld::Box> solid;
    VoxelWorld(world.octree()).query(lo, hi, solid);
    return solid.empty();
}

bool Level::setStart(const CellBox& box, float yaw) {
    if (!startFree(box)) return false;
    clearStart();
    start.set = true;
    start.box = box;
    start.yaw = yaw;
    start.lockId = world.lock(box);
    return true;
}

void Level::clearStart() {
    if (start.set) world.unlock(start.lockId);
    start = StartPoint{};
}
