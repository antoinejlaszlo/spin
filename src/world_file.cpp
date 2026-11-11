#include "world_file.hpp"
#include "dream.hpp"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <utility>
#include <vector>

namespace {

const char kMagic[8] = {'O', 'C', 'T', 'W', 'O', 'R', 'L', 'D'};
const uint32_t kVersion = 3;   // 3: instance (cage) voxels (VOXEL_INSTANCE, colour = cage id)
const uint64_t kMaxWords = 1ull << 30;   // sanity bound when reading

struct Writer {
    std::ofstream f;
    template <class T> void put(const T& v) { f.write(reinterpret_cast<const char*>(&v), sizeof v); }
    template <class T> void arr(const std::vector<T>& v) {
        put<uint64_t>(v.size());
        if (!v.empty()) f.write(reinterpret_cast<const char*>(v.data()), (std::streamsize)(v.size() * sizeof(T)));
    }
    void box(const CellBox& b) { put<int32_t>(b.level); for (int a = 0; a < 3; ++a) put<int32_t>(b.lo[a]); for (int a = 0; a < 3; ++a) put<int32_t>(b.hi[a]); }
};

struct Reader {
    std::ifstream f;
    bool ok = true;
    template <class T> T get() { T v{}; if (ok && !f.read(reinterpret_cast<char*>(&v), sizeof v)) ok = false; return v; }
    template <class T> bool arr(std::vector<T>& v) {
        uint64_t n = get<uint64_t>();
        if (!ok || n > kMaxWords) return ok = false;
        v.resize((size_t)n);
        if (n && !f.read(reinterpret_cast<char*>(v.data()), (std::streamsize)(n * sizeof(T)))) ok = false;
        return ok;
    }
    CellBox box() {
        CellBox b; b.level = get<int32_t>();
        for (int a = 0; a < 3; ++a) b.lo[a] = get<int32_t>();
        for (int a = 0; a < 3; ++a) b.hi[a] = get<int32_t>();
        if (b.level < 0 || b.level > VoxelStore::kMaxLevel) ok = false;
        return b;
    }
};

} // namespace

bool saveWorld(const std::string& path, LevelSet& levels, std::string& error) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path target(path);
    if (target.has_parent_path()) fs::create_directories(target.parent_path(), ec);
    const fs::path tmp = target.string() + ".tmp";
    {
        Writer w;
        w.f.open(tmp, std::ios::binary | std::ios::trunc);
        if (!w.f) { error = "cannot write " + tmp.string(); return false; }
        w.f.write(kMagic, 8);
        w.put<uint32_t>(kVersion);
        w.put<uint32_t>((uint32_t)levels.baseLevel);
        w.put<uint32_t>((uint32_t)levels.count());
        w.put<uint32_t>((uint32_t)levels.currentIndex());
        for (int i = 0; i < levels.count(); ++i) {
            Level& L = levels.at(i);
            w.put<uint32_t>((uint32_t)L.name.size());
            w.f.write(L.name.data(), (std::streamsize)L.name.size());
            Octree o;
            L.world.rawOctree(o);                        // instances as references; strip the editor selection
            for (size_t k = 0; k < o.colors.size(); ++k) {
                if (!(o.flags[k] & VOXEL_INSTANCE)) o.colors[k] |= 0xFF000000u;
                o.flags[k] &= (uint8_t)~VOXEL_SELECTED;
            }
            w.put<uint32_t>((uint32_t)o.depth);
            w.arr(o.nodes); w.arr(o.colors); w.arr(o.flags);
            w.put<uint32_t>((uint32_t)L.portals.open);
            for (const PortalDoor& d : L.portals.door) {
                w.put<uint8_t>(d.exists ? 1 : 0);
                w.box(d.wall);
                w.put<int32_t>(d.upAxis); w.put<int32_t>(d.upSign); w.put<int32_t>(d.normalAxis); w.put<int32_t>(d.normalSign);
            }
            w.put<uint8_t>(L.start.set ? 1 : 0);
            w.box(L.start.box);
            w.put<float>(L.start.yaw);
        }
        // The dream: its voxels and its cages.
        w.put<uint8_t>(levels.dream ? 1 : 0);
        if (levels.dream) {
            Octree o;
            levels.dream->level.world.rawOctree(o);
            for (size_t k = 0; k < o.colors.size(); ++k) {
                if (!(o.flags[k] & VOXEL_INSTANCE)) o.colors[k] |= 0xFF000000u;
                o.flags[k] &= (uint8_t)~VOXEL_SELECTED;
            }
            w.put<uint32_t>((uint32_t)o.depth);
            w.arr(o.nodes); w.arr(o.colors); w.arr(o.flags);
            w.put<uint32_t>((uint32_t)levels.dream->cages.size());
            for (const CellBox& c : levels.dream->cages) w.box(c);
        }
        w.f.flush();
        if (!w.f) { error = "error while writing " + tmp.string(); return false; }
    }
    fs::rename(tmp, target, ec);
    if (ec) { error = "cannot replace " + target.string() + ": " + ec.message(); return false; }
    return true;
}

bool loadWorld(const std::string& path, LevelSet& out, std::string& error) {
    Reader r;
    r.f.open(path, std::ios::binary);
    if (!r.f) { error = "cannot open " + path; return false; }
    char magic[8] = {};
    r.f.read(magic, 8);
    if (!r.f || std::memcmp(magic, kMagic, 8) != 0) { error = path + " is not a world file"; return false; }
    uint32_t version = r.get<uint32_t>();
    if (!r.ok || version < 1 || version > kVersion) { error = path + ": unsupported version " + std::to_string(version); return false; }
    LevelSet set;
    set.baseLevel = (int)r.get<uint32_t>();
    uint32_t count = r.get<uint32_t>(), current = r.get<uint32_t>();
    if (!r.ok || count == 0 || count > 4096 || set.baseLevel < 1 || set.baseLevel > VoxelStore::kMaxLevel) {
        error = path + ": bad header"; return false;
    }
    for (uint32_t i = 0; i < count && r.ok; ++i) {
        uint32_t len = r.get<uint32_t>();
        if (!r.ok || len > 4096) { r.ok = false; break; }
        std::string name(len, '\0');
        if (len && !r.f.read(&name[0], len)) { r.ok = false; break; }
        Octree o;
        o.depth = (int)r.get<uint32_t>();
        if (!r.arr(o.nodes) || !r.arr(o.colors) || !r.arr(o.flags)) break;
        if (o.nodes.size() < 2 || o.nodes.size() % 2 || o.colors.size() * 2 != o.nodes.size() ||
            o.flags.size() != o.colors.size() || o.depth < 0 || o.depth > VoxelStore::kMaxLevel) { r.ok = false; break; }
        Level& L = set.add(name, o);
        int open = (int)r.get<uint32_t>();
        for (int d = 0; d < 2; ++d) {
            bool exists = r.get<uint8_t>() != 0;
            CellBox wall = r.box();
            int ua = r.get<int32_t>(), us = r.get<int32_t>(), na = r.get<int32_t>(), ns = r.get<int32_t>();
            if (!r.ok) break;
            // Recreate the door: its black wall is already in the voxels; this
            // locks it and rebuilds the doorway. Keep the slot it was in.
            if (exists) {
                bool made = L.portals.create(L.world, wall, ua, us, na, ns, nullptr);
                if (!made) std::fprintf(stderr, "%s: level '%s': door %d could not be restored\n", path.c_str(), name.c_str(), d);
                // A lone second door goes to slot 0 when created: put it back in slot 1.
                if (made && d == 1 && !L.portals.door[1].exists) std::swap(L.portals.door[0], L.portals.door[1]);
            }
        }
        L.portals.open = open == 1 ? 1 : 0;
        bool startSet = r.get<uint8_t>() != 0;
        CellBox sb = r.box();
        float yaw = r.get<float>();
        if (!r.ok) break;
        if (startSet && !L.setStart(sb, yaw))
            std::fprintf(stderr, "%s: level '%s': the start position is blocked, not restored\n", path.c_str(), name.c_str());
        L.world.commit();
    }
    if (r.ok && version >= 2 && r.get<uint8_t>() != 0 && r.ok) {
        Octree o;
        o.depth = (int)r.get<uint32_t>();
        if (r.arr(o.nodes) && r.arr(o.colors) && r.arr(o.flags)) {
            if (o.nodes.size() < 2 || o.nodes.size() % 2 || o.colors.size() * 2 != o.nodes.size() || o.flags.size() != o.colors.size())
                r.ok = false;
            else {
                auto d = std::make_shared<Dream>(o);
                uint32_t n = r.get<uint32_t>();
                if (!r.ok || n > 65536) r.ok = false;
                for (uint32_t i = 0; i < n && r.ok; ++i) d->cages.push_back(r.box());
                if (r.ok) set.dream = std::move(d);
            }
        }
    }
    if (!r.ok) { error = path + ": truncated or corrupt file"; return false; }
    linkDream(set);                      // instances find their content in the dream
    set.select((int)current);
    out = std::move(set);
    return true;
}
