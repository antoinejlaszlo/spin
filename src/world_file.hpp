#pragma once
// World files: a whole world (a LevelSet: every level with its voxels, portal
// doors and start position) in one binary file.
//
// Format (little-endian), version 3 (2: no instance voxels; 1: no dream either):
//   "OCTWORLD"  u32 version  u32 baseLevel  u32 levelCount  u32 currentLevel
//   per level:
//     u32 nameLength, name bytes
//     octree: u32 depth, u64 nodeWords, u32[nodeWords], u64 colors, u32[colors], u64 flags, u8[flags]
//     portals: u32 open, then 2 doors: u8 exists, cellbox, i32 upAxis upSign normalAxis normalSign
//     start:   u8 set, cellbox, f32 yaw
//   u8 hasDream; if 1: octree (as above), u32 cageCount, cellbox[cageCount]
//   cellbox = i32 level, i32 lo[3], i32 hi[3]
// Octrees are stored raw: an instance (cage) voxel is a leaf with flag
// VOXEL_INSTANCE whose colour is the cage id (see VoxelStore::toOctree).
// Editor-only state (the move tool's selection, edit history) is not saved.
#include <string>
#include "level.hpp"

// Writes all levels (commits pending edits first). The file is written next
// to `path` and renamed over it, so a failed save never destroys the old one.
bool saveWorld(const std::string& path, LevelSet& levels, std::string& error);
// Reads a world into `out` (replacing its contents). On failure `out` is untouched.
// A file without a dream (version 1) gives out.dream == nullptr.
bool loadWorld(const std::string& path, LevelSet& out, std::string& error);
