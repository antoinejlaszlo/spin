// Real-time window: GLFW for the window/input, plain OpenGL 1.1 to show the
// frame as a full-screen texture (no GL loader needed).
#include "viewer.hpp"
#include "physics.hpp"
#include "orientation.hpp"
#include "build_tool.hpp"
#include "build_ui.hpp"
#include "portal_tool.hpp"
#include "portals.hpp"
#include "start_tool.hpp"
#include "move_tool.hpp"
#include "world_file.hpp"
#include "dream.hpp"
#include "object_tool.hpp"
#include "level.hpp"
#include "world.hpp"

#include <GLFW/glfw3.h>   // also includes the platform's GL/gl.h

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#ifndef GL_CLAMP_TO_EDGE
  #define GL_CLAMP_TO_EDGE 0x812F
#endif

namespace {

struct State {
    Camera cam;                 // position; orientation comes from `view`
    PlayerView view;            // body frame + mouse look + air-rotation transitions
    RenderSettings settings;
    float renderScale = 1.0f;   // internal resolution relative to the window
    float moveSpeed = 0.35f;    // fly speed, scene units per second (the scene is 1 unit wide)
    Player* player = nullptr;
    bool walking = true;        // sphere physics (false = free fly / noclip)
    bool captured = false;
    bool firstMouse = true;
    double lastX = 0, lastY = 0;
    int seenRespawns = 0;       // player->respawns already handled (view reset)
    // Levels: the current one is edited (edit mode) or played (play mode).
    LevelSet* levels = nullptr;
    Level* level = nullptr;
    std::unique_ptr<VoxelWorld> collide;   // physics view of level->world
    std::unique_ptr<Player> playerObj;
    // Editing (current level's world and portals)
    World* world = nullptr;
    Renderer* renderer = nullptr;
    BuildTool* tool = nullptr;
    PortalTool* ptool = nullptr;
    StartTool* stool = nullptr;
    MoveTool* mtool = nullptr;
    ObjectTool* otool = nullptr;
    // The dream (F6): another world, with cages to build models in.
    bool inDream = false;
    int hoverCage = -1;         // cage tool: the cage under the reticle
    struct Place { bool valid = false, walking = true; Camera cam; PlayerView view; Vec3 pos; };
    Place levelPlace, dreamPlace;   // where you were on each side
    // Player size: 2^sizeStep times the normal size (PageUp / PageDown).
    int sizeStep = 0;
    MoveTool::Config moveCfg;   // the move tool's sizes at normal size
    bool worldDirty = false;    // committed (and uploaded) once per frame
    std::string worldPath;      // the world file (Ctrl+S / Ctrl+O)
    Portals* portals = nullptr;
    bool menuOpen = false;      // build menu (Tab): cursor free, clicks pick colour / size / grip
    bool keyLog = false;        // --keylog: print every key event to the console
    // Play mode: no tools; the player starts at the level's start position.
    bool playing = false;
    struct EditSnapshot { bool walking; Camera cam; PlayerView view; Vec3 pos; } editSnap{};
    std::string notice;         // short message shown in the title bar
    // Full screen (F11): the windowed placement to come back to.
    bool fullscreen = false, vsync = false;
    int winX = 100, winY = 100, winW = 1280, winH = 720;
    double noticeUntil = 0;
};

void notify(State& s, const char* msg) {
    std::printf("%s\n", msg);
    s.notice = msg;
    s.noticeUntil = glfwGetTime() + 3.0;
}

// Applies an edit made by the build tool and uploads the result.
void applyEdit(State& s, Edit e) {
    if (e.op == Edit::PLACE && s.walking) {   // never build inside the player
        e.hasKeepOut = true;
        e.keepOut[0] = s.player->pos.x; e.keepOut[1] = s.player->pos.y; e.keepOut[2] = s.player->pos.z;
        e.keepOut[3] = s.player->cfg.radius;
    }
    if (s.world->apply(e) && s.world->commit()) s.renderer->updateOctree(s.world->octree());
}

void commitWorld(State& s) {
    if (s.world->commit()) s.renderer->updateOctree(s.world->octree());
}

void setMenu(GLFWwindow* w, State& s, bool open);

const char* modeName(int m) {
    switch (m) {
    case RENDER_NORMALS: return "normals";
    case RENDER_STEPS:   return "steps";
    case RENDER_DEPTH:   return "depth";
    default:             return "shaded";
    }
}

void setCaptured(GLFWwindow* w, State& s, bool on) {
    s.captured = on;
    s.firstMouse = true;
    glfwSetInputMode(w, GLFW_CURSOR, on ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    if (glfwRawMouseMotionSupported())
        glfwSetInputMode(w, GLFW_RAW_MOUSE_MOTION, on ? GLFW_TRUE : GLFW_FALSE);
}

// Close enough that the fisheye curvature is obvious.
Camera startCamera() { return defaultCamera(35.0f, 0.6f); }

// Where the walking player starts in the editor when the level has no start
// position: above the middle, facing the centre.
const Vec3 kSpawn = {1.25f, 1.45f, 1.25f};

// Where (and facing where) the player respawns: the level's start position
// in play mode, and in the editor when it is set.
Vec3 spawnPoint(const State& s, float& yaw, float& pitch) {
    if (s.inDream) { pitch = 0.1f; return s.levels->dream->spawn(s.player->cfg.radius, yaw); }
    if (s.level->start.set) {
        yaw = s.level->start.yaw; pitch = 0.0f;
        return s.level->start.eye(s.player->cfg.radius);
    }
    yaw = std::atan2(1.5f - kSpawn.z, 1.5f - kSpawn.x); pitch = 0.15f;
    return kSpawn;
}

void spawnPlayer(State& s) {
    float yaw, pitch;
    s.player->setSpawn(spawnPoint(s, yaw, pitch));
    s.player->respawn();
    s.seenRespawns = s.player->respawns;
    s.view.reset(yaw, pitch);   // upright again
}

// Points the editor / game at a level: its octree goes to the renderer, and
// physics collide with it. (Called once at start; switching levels later is
// just another call.)
void applySize(State& s, bool announce, bool force = false);

// Points the editor at a world to edit: the current level, or the dream.
void bindTarget(State& s, Level& target, bool upload) {
    s.level = &target;
    s.world = &s.level->world;
    s.portals = &s.level->portals;
    s.collide = std::make_unique<VoxelWorld>(s.world->octree());
    s.playerObj = std::make_unique<Player>(*s.collide, kSpawn);
    s.player = s.playerObj.get();
    s.player->portals = s.portals;
    if (upload) {
        // Cage voxels show the dream's current content: re-resolve (the dream
        // may have changed while we were elsewhere).
        s.world->markDirty();
        s.world->commit();
        s.renderer->updateOctree(s.world->octree());
        s.otool->refreshGhost(s.levels->dream.get());
        if (s.otool->holding()) s.renderer->setGhost(&s.otool->held().ghost);
    }
    s.tool->cancel(); s.ptool->cancel();
    // Which tools exist here; in the dream, building stays inside the cages.
    unsigned avail = 1u << BuildTool::TOOL_BLOCKS;
    if (s.inDream) avail |= (1u << BuildTool::TOOL_CAGE) | (1u << BuildTool::TOOL_REFERENCE);
    else avail |= (1u << BuildTool::TOOL_PORTAL) | (1u << BuildTool::TOOL_START) | (1u << BuildTool::TOOL_MOVE);
    if (s.otool->holding()) avail |= 1u << BuildTool::TOOL_OBJECT;      // levels and dream (into cages)
    s.tool->setAvailable(avail);
    s.tool->setAllowed(s.inDream ? &s.levels->dream->cages : nullptr);
    applySize(s, false);
}

void bindLevel(State& s, int index, bool upload) {
    s.levels->select(index);
    s.inDream = false;
    bindTarget(s, s.levels->current(), upload);
}

void resetFlyCamera(State& s) {
    s.cam = startCamera();
    s.view.reset(s.cam.yaw, s.cam.pitch);
}

// In the air after a jump, the move keys rotate the player instead.
bool airRotating(const State& s) {
    return s.walking && s.player->jumped && !s.player->grounded;
}

void toggleWalk(State& s) {
    s.walking = !s.walking;
    if (s.walking) {   // drop the player where the camera is
        s.player->setSpawn({s.cam.pos[0], s.cam.pos[1], s.cam.pos[2]});
        s.player->respawn();
        s.seenRespawns = s.player->respawns;
        float yaw, pitch;
        s.player->setSpawn(spawnPoint(s, yaw, pitch));   // falling off still respawns at the start
    }
}

void changeFov(State& s, float dir) {
    if (s.settings.projection == PROJ_FISHEYE_HOST)
        s.settings.fisheyeFovDeg = std::clamp(s.settings.fisheyeFovDeg + dir * 20.0f, 60.0f, 360.0f);
    else
        s.settings.fovDeg = std::clamp(s.settings.fovDeg + dir * 10.0f, 20.0f, 150.0f);
}

float activeFov(const State& s) {
    return s.settings.projection == PROJ_FISHEYE_HOST ? s.settings.fisheyeFovDeg : s.settings.fovDeg;
}

// Air rotation, after a jump:
//  - the movement keys, by position (W/A/S/D on QWERTY = Z/Q/S/D on AZERTY):
//    forward / back pitch down / up, left / right roll, and the keys beside
//    them (Q/E on QWERTY = A/E on AZERTY) yaw;
//  - alternatively, by the letter printed on the key: O / L pitch, K / M roll,
//    I / P yaw.
bool airTurnKey(int key, int scancode, PlayerView::Turn& turn) {
    const char* name = glfwGetKeyName(key, scancode);
    if (!name || !name[0] || name[1]) return false;
    switch (name[0] | 0x20) {   // lower case
    case 'o': turn = PlayerView::PITCH_DOWN; return true;
    case 'l': turn = PlayerView::PITCH_UP;   return true;
    case 'k': turn = PlayerView::ROLL_LEFT;  return true;
    case 'm': turn = PlayerView::ROLL_RIGHT; return true;
    case 'i': turn = PlayerView::YAW_LEFT;   return true;
    case 'p': turn = PlayerView::YAW_RIGHT;  return true;
    default: return false;
    }
}

bool keyLetterIs(int key, int scancode, char c) {
    const char* name = glfwGetKeyName(key, scancode);
    return name && name[0] && !name[1] && (name[0] | 0x20) == c;
}

const char* toolName(int t) {
    return t == BuildTool::TOOL_NONE ? "none" : t == BuildTool::TOOL_PORTAL ? "portal" : t == BuildTool::TOOL_START ? "start"
         : t == BuildTool::TOOL_MOVE ? "move" : t == BuildTool::TOOL_CAGE ? "cage (copy)"
         : t == BuildTool::TOOL_REFERENCE ? "reference (cage voxel)"
         : t == BuildTool::TOOL_OBJECT ? "object" : "blocks";
}

// F11: full screen on the monitor the window is mostly on, and back.
void setFullscreen(GLFWwindow* w, State& s, bool on) {
    if (on == s.fullscreen) return;
    if (on) {
        glfwGetWindowPos(w, &s.winX, &s.winY);
        glfwGetWindowSize(w, &s.winW, &s.winH);
        GLFWmonitor* best = glfwGetPrimaryMonitor();
        int count = 0, bestArea = -1;
        GLFWmonitor** mons = glfwGetMonitors(&count);
        for (int i = 0; i < count; ++i) {                    // the monitor with most of the window
            int mx, my; glfwGetMonitorPos(mons[i], &mx, &my);
            const GLFWvidmode* m = glfwGetVideoMode(mons[i]);
            if (!m) continue;
            int ox = std::max(0, std::min(s.winX + s.winW, mx + m->width) - std::max(s.winX, mx));
            int oy = std::max(0, std::min(s.winY + s.winH, my + m->height) - std::max(s.winY, my));
            if (ox * oy > bestArea) { bestArea = ox * oy; best = mons[i]; }
        }
        const GLFWvidmode* mode = best ? glfwGetVideoMode(best) : nullptr;
        if (!mode) { notify(s, "Full screen is not available"); return; }
        glfwSetWindowMonitor(w, best, 0, 0, mode->width, mode->height, mode->refreshRate);
    } else {
        glfwSetWindowMonitor(w, nullptr, s.winX, s.winY, s.winW, s.winH, 0);
    }
    s.fullscreen = on;
    glfwSwapInterval(s.vsync ? 1 : 0);                       // some drivers reset it on a mode change
}

// The selection belongs to the move tool: dropped when another tool (or play) takes over.
void dropSelection(State& s) {
    if (s.mtool->deselect(*s.world)) s.worldDirty = true;
}

// V: blocks -> portal -> start -> move -> blocks.
void toggleTool(State& s) {
    s.ptool->cancel();
    dropSelection(s);
    s.tool->nextTool();
    std::printf("tool: %s\n", toolName(s.tool->tool()));
}

// Player size (PageUp / PageDown): the player, their reach and the blocks
// they place all scale by powers of two.
constexpr int kMinSize = -4, kMaxSize = 2;

// force: change the player's size even without room (they are moved right after).
void applySize(State& s, bool announce, bool force) {
    int step = s.playing ? 0 : s.sizeStep;
    if (!s.player->setScale(std::ldexp(1.0f, step), force)) {      // no room to grow here: stay as you are
        step = (int)std::lround(std::log2(s.player->scale()));
        if (!s.playing) s.sizeStep = step;
        if (announce) { notify(s, "No room to grow here"); announce = false; }
    }
    const float k = std::ldexp(1.0f, step);
    s.tool->setScaleShift(step);
    s.tool->cfg.reach = 0.35f * k;
    s.tool->cfg.voidDistance = 0.06f * k;
    s.ptool->cfg.reach = 0.35f * k;
    s.stool->reach = 0.35f * k;
    s.otool->reach = 0.35f * k;
    s.otool->voidDistance = 0.06f * k;
    MoveTool::Config& m = s.mtool->cfg;
    m = s.moveCfg;
    m.reach *= k; m.arrowMargin *= k; m.arrowWidth *= k; m.pickWidth *= k; m.knob *= k;
    if (announce) {
        char msg[96];
        std::snprintf(msg, sizeof msg, step >= 0 ? "Size x%d" : "Size 1/%d", 1 << std::abs(step));
        notify(s, msg);
    }
}

void changeSize(State& s, int d) {
    if (s.playing) return;
    const int want = std::clamp(s.sizeStep + d, kMinSize, kMaxSize);
    if (want == s.sizeStep) return;
    s.sizeStep = want;
    applySize(s, true);
}

// Remembers / restores where you are, for going back and forth to the dream.
State::Place currentPlace(const State& s) {
    State::Place p;
    p.valid = true; p.walking = s.walking; p.cam = s.cam; p.view = s.view; p.pos = s.player->pos;
    return p;
}

void restorePlace(State& s, const State::Place& p) {
    float yaw, pitch;
    if (!p.valid) {                          // first visit: the default spot
        s.walking = true;
        spawnPlayer(s);
        return;
    }
    s.walking = p.walking; s.cam = p.cam; s.view = p.view;
    s.player->setSpawn(p.pos);
    s.player->respawn();
    s.seenRespawns = s.player->respawns;
    s.player->setSpawn(spawnPoint(s, yaw, pitch));
}

// F6: to the dream and back (editor only). Each side keeps where you were.
void setDream(GLFWwindow* w, State& s, bool on);

void setMenu(GLFWwindow* w, State& s, bool open);

// Ctrl+S: writes every level to the world file.
void saveWorldFile(State& s) {
    std::string err;
    char msg[512];
    if (saveWorld(s.worldPath, *s.levels, err))
        std::snprintf(msg, sizeof msg, "Saved %d level(s) to %s", s.levels->count(), s.worldPath.c_str());
    else
        std::snprintf(msg, sizeof msg, "Save failed: %s", err.c_str());
    notify(s, msg);
}

void setPlaying(GLFWwindow* w, State& s, bool on);

void setDream(GLFWwindow* w, State& s, bool on) {
    if (on == s.inDream || !s.levels->dream) return;
    if (s.playing) { notify(s, "The dream is reached from the editor (F5 first)"); return; }
    if (s.menuOpen) setMenu(w, s, false);
    s.tool->cancel(); s.ptool->cancel(); s.mtool->release();
    dropSelection(s);
    if (s.worldDirty) { commitWorld(s); s.worldDirty = false; }
    (on ? s.levelPlace : s.dreamPlace) = currentPlace(s);
    s.inDream = on;
    bindTarget(s, on ? s.levels->dream->level : s.levels->current(), true);
    restorePlace(s, on ? s.dreamPlace : s.levelPlace);
    s.hoverCage = -1;
    notify(s, on ? "THE DREAM: build inside the cages; the cage tool (V) takes a cage's model back (F6: back)"
                 : "Back in the level editor (F6: the dream)");
}

// Cage tool: takes a copy of the aimed cage's model back to the level, in
// hand. Reference tool (asReference): takes a cage voxel instead, a pointer
// to the cage that keeps showing what it holds.
void takeCage(GLFWwindow* w, State& s, bool asReference) {
    if (!s.inDream || s.hoverCage < 0) return;
    Dream& d = *s.levels->dream;
    if (s.worldDirty) { commitWorld(s); s.worldDirty = false; }
    Model m = asReference ? makeReference(d, s.hoverCage)
                          : extractModel(d.level.world, d.cages[s.hoverCage], Dream::kCageLevel, &d);
    if (m.empty() || (asReference && extractModel(d.level.world, d.cages[s.hoverCage], Dream::kCageLevel, &d).empty())) {
        notify(s, "This cage is empty: build something in it first");
        return;
    }
    char name[64];
    std::snprintf(name, sizeof name, asReference ? "cage voxel -> cage %d" : "copy of cage %d", s.hoverCage + 1);
    m.name = name;
    s.otool->hold(std::move(m));
    s.renderer->setGhost(&s.otool->held().ghost);
    setDream(w, s, false);
    s.tool->setTool(BuildTool::TOOL_OBJECT);
    char msg[160];
    if (asReference)
        std::snprintf(msg, sizeof msg, "Holding a %s: right click places it (it shows the cage's content, live)", name);
    else
        std::snprintf(msg, sizeof msg, "Holding a %s (%zu voxels): right click places it", name, s.otool->held().leaves.size());
    notify(s, msg);
}

// Ctrl+O: replaces all levels with the world file's (unsaved changes are lost).
void loadWorldFile(GLFWwindow* w, State& s) {
    LevelSet loaded;
    std::string err;
    if (!loadWorld(s.worldPath, loaded, err)) { notify(s, ("Load failed: " + err).c_str()); return; }
    setPlaying(w, s, false);
    setDream(w, s, false);
    if (s.menuOpen) setMenu(w, s, false);
    s.mtool->deselect(*s.world);                 // the old level's selection goes away with it
    s.tool->cancel(); s.ptool->cancel(); s.mtool->release();
    const int base = loaded.baseLevel;
    if (!loaded.dream) loaded.dream = Dream::create(loaded.baseLevel);
    linkDream(loaded);
    *s.levels = std::move(loaded);
    s.dreamPlace = State::Place{};
    bindLevel(s, s.levels->currentIndex(), true);
    s.worldDirty = false;
    if (s.walking) spawnPlayer(s);
    char msg[512];
    std::snprintf(msg, sizeof msg, "Loaded %d level(s) from %s%s", s.levels->count(), s.worldPath.c_str(),
                  base != s.tool->level() - s.tool->sizeStep() ? " (different block size: restart to build at its size)" : "");
    notify(s, msg);
}

// F5: editor <-> play mode. Play needs a start position; leaving play puts
// you back where you were in the editor.
void setPlaying(GLFWwindow* w, State& s, bool on) {
    if (on == s.playing) return;
    if (on) {
        if (s.inDream) { notify(s, "No play mode in the dream (F6: back to the level)"); return; }
        if (!s.level->playable()) { notify(s, "Set a start position first (start tool: V, or the Tab menu)"); return; }
        s.editSnap = {s.walking, s.cam, s.view, s.player->pos};
        if (s.menuOpen) setMenu(w, s, false);
        s.tool->cancel(); s.ptool->cancel();
        dropSelection(s);
        s.playing = true;
        s.walking = true;
        applySize(s, false, true);               // play at normal size (then to the start)
        spawnPlayer(s);
        notify(s, "PLAY mode (F5: back to the editor)");
    } else {
        s.playing = false;
        applySize(s, false, true);               // back to your editor size (then where you were)
        s.walking = s.editSnap.walking;
        s.cam = s.editSnap.cam;
        s.view = s.editSnap.view;
        float yaw, pitch;
        s.player->setSpawn(s.editSnap.pos);
        s.player->respawn();
        s.seenRespawns = s.player->respawns;
        s.player->setSpawn(spawnPoint(s, yaw, pitch));
        notify(s, "EDIT mode (F5: play)");
    }
}

void onKey(GLFWwindow* w, int key, int scancode, int action, int mods) {
    State& s = *static_cast<State*>(glfwGetWindowUserPointer(w));
    if (s.keyLog) {
        const char* name = glfwGetKeyName(key, scancode);
        std::printf("key %d scancode %d action %d mods %d name '%s'\n", key, scancode, action, mods, name ? name : "");
    }
    if (action != GLFW_PRESS) return;   // presses only: holding a key does not repeat turns
    if (key == GLFW_KEY_F11 || (key == GLFW_KEY_ENTER && (mods & GLFW_MOD_ALT))) { setFullscreen(w, s, !s.fullscreen); return; }
    if (key == GLFW_KEY_F5) { setPlaying(w, s, !s.playing); return; }
    if (key == GLFW_KEY_F6) { setDream(w, s, !s.inDream); return; }
    if (key == GLFW_KEY_PAGE_UP)   { changeSize(s, +1); return; }
    if (key == GLFW_KEY_PAGE_DOWN) { changeSize(s, -1); return; }
    if ((mods & GLFW_MOD_CONTROL) && !s.playing) {          // file: Ctrl+S save, Ctrl+O reload
        if (key == GLFW_KEY_S || keyLetterIs(key, scancode, 's')) { saveWorldFile(s); return; }
        if (key == GLFW_KEY_O || keyLetterIs(key, scancode, 'o')) { loadWorldFile(w, s); return; }
    }
    // Tool switch first, before any other key logic: the V key by position or by its letter.
    if (key == GLFW_KEY_V || keyLetterIs(key, scancode, 'v')) { if (!s.playing) toggleTool(s); return; }
    if (airRotating(s)) {
        PlayerView::Turn t;
        bool hit = true;
        switch (key) {
        case GLFW_KEY_W: t = PlayerView::PITCH_DOWN; break;
        case GLFW_KEY_S: t = PlayerView::PITCH_UP;   break;
        case GLFW_KEY_A: t = PlayerView::ROLL_LEFT;  break;
        case GLFW_KEY_D: t = PlayerView::ROLL_RIGHT; break;
        case GLFW_KEY_Q: t = PlayerView::YAW_LEFT;   break;
        case GLFW_KEY_E: t = PlayerView::YAW_RIGHT;  break;
        default: hit = false; break;
        }
        if (hit) { s.view.turn(t); return; }     // ignored while the previous turn's transition is running
    }
    PlayerView::Turn turn;
    if (airTurnKey(key, scancode, turn)) {
        if (airRotating(s)) s.view.turn(turn);   // ignored while the previous turn's transition is running
        return;                                  // these letters do nothing else
    }
    if (s.playing && (key == GLFW_KEY_TAB || key == GLFW_KEY_COMMA || key == GLFW_KEY_PERIOD || key == GLFW_KEY_N))
        return;                                  // editor-only keys
    switch (key) {
    case GLFW_KEY_TAB: setMenu(w, s, !s.menuOpen); break;
    case GLFW_KEY_COMMA:  s.tool->setSizeStep(s.tool->sizeStep() + 1); break;   // smaller blocks
    case GLFW_KEY_PERIOD: s.tool->setSizeStep(s.tool->sizeStep() - 1); break;   // bigger blocks
    case GLFW_KEY_ESCAPE:
        if (s.menuOpen) setMenu(w, s, false);
        else if (s.captured) setCaptured(w, s, false);
        else glfwSetWindowShouldClose(w, GLFW_TRUE);
        break;
    case GLFW_KEY_1: s.settings.mode = RENDER_SHADED; break;
    case GLFW_KEY_2: s.settings.mode = RENDER_NORMALS; break;
    case GLFW_KEY_3: s.settings.mode = RENDER_STEPS; break;
    case GLFW_KEY_4: s.settings.mode = RENDER_DEPTH; break;
    case GLFW_KEY_F: s.settings.shadows = !s.settings.shadows; break;
    case GLFW_KEY_T: s.settings.filter = !s.settings.filter; break;
    case GLFW_KEY_G:   // texel grid: 2, 4, 8, 16, smooth, 2, ...
        s.settings.filter = true;
        s.settings.texels = s.settings.texels == 0 ? 2 : (s.settings.texels >= 16 ? 0 : s.settings.texels * 2);
        break;
    case GLFW_KEY_F3: s.settings.lodPixels = s.settings.lodPixels > 0.0f ? 0.0f : 1.0f; break;
    case GLFW_KEY_LEFT_BRACKET:  s.renderScale = std::max(0.125f, s.renderScale * 0.5f); break;
    case GLFW_KEY_RIGHT_BRACKET: s.renderScale = std::min(1.0f, s.renderScale * 2.0f); break;
    case GLFW_KEY_R:
        if (s.walking) spawnPlayer(s);
        else resetFlyCamera(s);
        break;
    case GLFW_KEY_N: toggleWalk(s); break;
    case GLFW_KEY_F4:
        s.settings.projection = s.settings.projection == PROJ_FISHEYE_HOST ? PROJ_PERSPECTIVE_HOST : PROJ_FISHEYE_HOST;
        break;
    case GLFW_KEY_MINUS: case GLFW_KEY_KP_SUBTRACT: changeFov(s, -1.0f); break;
    case GLFW_KEY_EQUAL: case GLFW_KEY_KP_ADD:      changeFov(s, +1.0f); break;
    default: break;
    }
}

void setMenu(GLFWwindow* w, State& s, bool open) {
    if (s.playing) open = false;
    s.menuOpen = open;
    s.tool->cancel();
    s.ptool->cancel();
    setCaptured(w, s, !open);
}

// Build menu clicks use framebuffer pixels (the cursor is in window coordinates).
void cursorFb(GLFWwindow* w, float& x, float& y) {
    double cx, cy; int ww, wh, fw, fh;
    glfwGetCursorPos(w, &cx, &cy);
    glfwGetWindowSize(w, &ww, &wh);
    glfwGetFramebufferSize(w, &fw, &fh);
    x = (float)(cx * (ww ? (double)fw / ww : 1.0));
    y = (float)(cy * (wh ? (double)fh / wh : 1.0));
}

void onMouseButton(GLFWwindow* w, int button, int action, int) {
    State& s = *static_cast<State*>(glfwGetWindowUserPointer(w));
    if (s.menuOpen) {
        if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
            int fw, fh; glfwGetFramebufferSize(w, &fw, &fh);
            float x, y; cursorFb(w, x, y);
            buildUiClick(*s.tool, fw, fh, x, y);
        }
        return;
    }
    if (!s.captured) {
        if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) setCaptured(w, s, true);
        return;
    }
    if (s.playing) return;                        // no tools in play mode
    // Left = remove, right = place. Press anchors, release applies: a click is
    // one block, a drag covers the (bounded) box between the two cells.
    BuildTool::Action a = button == GLFW_MOUSE_BUTTON_LEFT ? BuildTool::REMOVE
                        : button == GLFW_MOUSE_BUTTON_RIGHT ? BuildTool::PLACE : BuildTool::NONE;
    if (a == BuildTool::NONE) return;
    if (s.tool->tool() == BuildTool::TOOL_NONE) return;     // no tool: clicks only look around
    if (s.tool->tool() == BuildTool::TOOL_CAGE || s.tool->tool() == BuildTool::TOOL_REFERENCE) {   // either button
        if (action == GLFW_PRESS) takeCage(w, s, s.tool->tool() == BuildTool::TOOL_REFERENCE);
        return;
    }
    if (s.tool->tool() == BuildTool::TOOL_OBJECT) {        // right: place a copy / a cage voxel
        if (action == GLFW_PRESS && a == BuildTool::PLACE) {
            if (s.otool->place(*s.world) > 0) {
                s.worldDirty = true;
                char msg[128]; std::snprintf(msg, sizeof msg, "Placed a %s", s.otool->held().name.c_str());
                notify(s, msg);
            } else {
                char msg[128]; std::snprintf(msg, sizeof msg, "Can't place it there: %s", s.otool->why());
                notify(s, msg);
            }
        }
        return;
    }
    if (s.tool->tool() == BuildTool::TOOL_MOVE) {
        if (a == BuildTool::REMOVE) {                       // left: grab an arrow or select
            if (action == GLFW_PRESS) {
                if (s.mtool->press(*s.world)) s.worldDirty = true;
                if (!s.mtool->message().empty()) notify(s, s.mtool->message().c_str());
            } else if (action == GLFW_RELEASE) {
                s.mtool->release();
            }
        } else if (action == GLFW_PRESS) {                  // right: deselect
            s.mtool->release();
            dropSelection(s);
        }
        return;
    }
    if (s.tool->tool() == BuildTool::TOOL_START) {
        if (action != GLFW_PRESS) return;
        if (a == BuildTool::PLACE) {
            if (s.stool->place(*s.level)) {
                float yaw, pitch;
                s.player->setSpawn(spawnPoint(s, yaw, pitch));
                notify(s, "Start position set (F5: play)");
            } else if (s.stool->hasCandidate()) {
                notify(s, "The start box needs a free spot on a floor");
            }
        } else if (s.stool->remove(*s.level)) {
            float yaw, pitch;
            s.player->setSpawn(spawnPoint(s, yaw, pitch));
            notify(s, "Start position removed");
        }
        return;
    }
    if (s.tool->tool() == BuildTool::TOOL_PORTAL) {
        if (action == GLFW_PRESS) {
            if (s.ptool->dragging() != BuildTool::NONE) s.ptool->cancel();
            else s.ptool->begin(a, *s.portals);
        } else if (action == GLFW_RELEASE) {
            float keep[4] = {s.player->pos.x, s.player->pos.y, s.player->pos.z, s.player->cfg.radius};
            if (s.ptool->end(a, *s.portals, *s.world, s.walking ? keep : nullptr)) commitWorld(s);
        }
        return;
    }
    if (action == GLFW_PRESS) {
        if (s.tool->dragging() != BuildTool::NONE) s.tool->cancel();   // both buttons: cancel
        else s.tool->begin(a);
    } else if (action == GLFW_RELEASE) {
        Edit e;
        if (s.tool->end(a, e)) applyEdit(s, e);
    }
}

void onCursor(GLFWwindow* w, double x, double y) {
    State& s = *static_cast<State*>(glfwGetWindowUserPointer(w));
    if (!s.captured) return;
    if (s.firstMouse) { s.lastX = x; s.lastY = y; s.firstMouse = false; return; }
    const float sens = 0.0022f;
    if (s.mtool->dragging()) {   // dragging a move-tool arrow: the mouse moves the object, not the view
        V3f f, u;
        s.view.visual(f, u);
        float rr[3] = {f.y * u.z - f.z * u.y, f.z * u.x - f.x * u.z, f.x * u.y - f.y * u.x};   // right = front x up
        float uu[3] = {u.x, u.y, u.z};
        float keep[4] = {s.player->pos.x, s.player->pos.y, s.player->pos.z, s.player->cfg.radius};
        if (s.mtool->drag((float)(x - s.lastX), (float)(y - s.lastY), s.cam.pos, rr, uu, *s.world,
                          s.walking ? keep : nullptr))
            s.worldDirty = true;
        s.lastX = x; s.lastY = y;
        return;
    }
    s.view.look((float)(x - s.lastX) * sens, -(float)(y - s.lastY) * sens);
    s.lastX = x; s.lastY = y;
}

void onScroll(GLFWwindow* w, double, double dy) {
    State& s = *static_cast<State*>(glfwGetWindowUserPointer(w));
    s.moveSpeed = std::clamp(s.moveSpeed * std::pow(1.25f, (float)dy), 0.005f, 5.0f);
}

// The physical view and body up, for walking on whatever surface we stand on.
void viewInput(const State& s, PlayerInput& in) {
    V3f f, r, u;
    s.view.physical(f, r, u);
    V3f b = s.view.bodyUp();
    in.hasView = true;
    in.viewF = {f.x, f.y, f.z}; in.viewR = {r.x, r.y, r.z}; in.viewU = {u.x, u.y, u.z};
    in.bodyUp = {b.x, b.y, b.z};
}

void walk(GLFWwindow* w, State& s, float dt) {
    auto down = [&](int k) { return glfwGetKey(w, k) == GLFW_PRESS; };
    PlayerInput in;
    if (airRotating(s)) {
        // move keys rotate now (see onKey); in the air nothing steers anyway
    } else {
        in.forward = (down(GLFW_KEY_W) ? 1.0f : 0.0f) - (down(GLFW_KEY_S) ? 1.0f : 0.0f);
        in.strafe  = (down(GLFW_KEY_D) ? 1.0f : 0.0f) - (down(GLFW_KEY_A) ? 1.0f : 0.0f);
    }
    in.jump = down(GLFW_KEY_SPACE);
    in.sprint = down(GLFW_KEY_LEFT_SHIFT);
    viewInput(s, in);
    s.player->update(dt, in);
    float m[9];
    if (s.player->takeRotation(m)) s.view.applyRotation(m);   // went through a portal
    if (s.player->needsUpright()) s.view.landUpright();   // landed on normal ground while rotated
    if (s.player->respawns != s.seenRespawns) {             // fell off the world: start over, upright
        s.seenRespawns = s.player->respawns;
        float yaw, pitch;
        spawnPoint(s, yaw, pitch);
        s.view.reset(yaw, pitch);
    }
    Vec3 e = s.player->eye();
    s.cam.pos[0] = e.x; s.cam.pos[1] = e.y; s.cam.pos[2] = e.z;
}

void fly(GLFWwindow* w, State& s, float dt) {
    V3f fv, rv, uv;
    s.view.physical(fv, rv, uv);
    float f[3] = {fv.x, fv.y, fv.z}, r[3] = {rv.x, rv.y, rv.z};
    float v[3] = {0, 0, 0};
    auto add = [&](const float* d, float k) { for (int i = 0; i < 3; ++i) v[i] += d[i] * k; };
    const float up[3] = {0, 1, 0};
    if (glfwGetKey(w, GLFW_KEY_W) == GLFW_PRESS) add(f, 1);
    if (glfwGetKey(w, GLFW_KEY_S) == GLFW_PRESS) add(f, -1);
    if (glfwGetKey(w, GLFW_KEY_D) == GLFW_PRESS) add(r, 1);
    if (glfwGetKey(w, GLFW_KEY_A) == GLFW_PRESS) add(r, -1);
    if (glfwGetKey(w, GLFW_KEY_SPACE) == GLFW_PRESS || glfwGetKey(w, GLFW_KEY_E) == GLFW_PRESS) add(up, 1);
    if (glfwGetKey(w, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS || glfwGetKey(w, GLFW_KEY_Q) == GLFW_PRESS) add(up, -1);
    float k = s.moveSpeed * dt * std::ldexp(1.0f, s.sizeStep);
    if (glfwGetKey(w, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) k *= 4.0f;
    Vec3 before{s.cam.pos[0], s.cam.pos[1], s.cam.pos[2]};
    for (int i = 0; i < 3; ++i) s.cam.pos[i] += v[i] * k;
    Vec3 after{s.cam.pos[0], s.cam.pos[1], s.cam.pos[2]};
    if (s.portals->crosses(before, after)) {   // flying through the open door works too
        float m[9];
        s.portals->rotation(m);
        Vec3 p = s.portals->mapPoint(after);
        s.cam.pos[0] = p.x; s.cam.pos[1] = p.y; s.cam.pos[2] = p.z;
        s.view.applyRotation(m);
        s.portals->swapOpen();
    }
}

} // namespace

int runViewer(Renderer& renderer, LevelSet& levels, const ViewerOptions& opt) {
    if (levels.count() == 0) { std::fprintf(stderr, "no level to edit\n"); return 1; }
    if (!glfwInit()) { std::fprintf(stderr, "glfwInit failed\n"); return 1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);   // legacy context: GL 1.1 calls are enough
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    GLFWwindow* win = glfwCreateWindow(opt.width, opt.height, "Octree renderer", nullptr, nullptr);
    if (!win) { std::fprintf(stderr, "could not create a window\n"); glfwTerminate(); return 1; }
    glfwMakeContextCurrent(win);
    glfwSwapInterval(opt.vsync ? 1 : 0);

    BuildTool tool(opt.buildLevel);
    PortalTool ptool;
    StartTool stool;
    MoveTool mtool;
    ObjectTool otool;
    State s;
    s.mtool = &mtool;
    s.otool = &otool;
    s.moveCfg = mtool.cfg;
    s.ptool = &ptool;
    s.stool = &stool;
    s.keyLog = opt.keyLog;
    s.worldPath = opt.worldPath;
    s.vsync = opt.vsync;
    s.renderer = &renderer;
    s.tool = &tool;
    s.levels = &levels;
    if (!levels.dream) { levels.dream = Dream::create(levels.baseLevel); linkDream(levels); }
    bindLevel(s, levels.currentIndex(), false);   // the renderer was created with this level's octree
    resetFlyCamera(s);
    s.walking = !opt.startFlying;
    if (s.walking) spawnPlayer(s);
    s.settings = opt.settings;
    s.renderScale = std::clamp(opt.renderScale, 0.125f, 1.0f);
    glfwSetWindowUserPointer(win, &s);
    glfwSetKeyCallback(win, onKey);
    if (opt.fullscreen) setFullscreen(win, s, true);
    glfwSetMouseButtonCallback(win, onMouseButton);
    glfwSetCursorPosCallback(win, onCursor);
    glfwSetScrollCallback(win, onScroll);
    setCaptured(win, s, true);

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glEnable(GL_TEXTURE_2D);

    std::printf("EDIT mode (level editor). F5 switches to PLAY mode (needs a start position) and back.\n"
                "World file: %s (Ctrl+S save, Ctrl+O reload it). F11 (or Alt+Enter): full screen\n"
                "Controls: mouse look | WASD (ZQSD on AZERTY) move | Space jump | Shift sprint | N walk/fly (noclip, editor)\n"
                "          after a jump, in the air: W/S (Z/S) pitch, A/D (Q/D) roll, Q/E (A/E) yaw by 90 degrees\n"
                "          (also O/L pitch, K/M roll, I/P yaw)\n"
                "          fly: Space/E up, Ctrl/Q down, Shift fast, wheel speed\n"
                "          1-4 shaded/normals/steps/depth | F shadows | F3 LOD | [ ] resolution | R respawn/reset\n"
                "          F4 fisheye/perspective | - = field of view | T colour filtering | G texel grid\n"
                "          Esc release mouse (click to capture again), Esc again to quit\n"
                "  build:  right click place, left click remove, hold + drag for a box (max %d per side)\n"
                "          Tab build menu (tool, colour, grip, block size) | , and . smaller / bigger blocks\n"
                "  portal: V switches to the portal tool: right drag places a door (corner to corner,\n"
                "          standing on the aimed face, facing you; the 2nd door has the 1st's size),\n"
                "          left click on a door removes it\n"
                "  start:  V again: right click on a floor sets the start position (facing your view),\n"
                "          left click on it removes it.\n"
                "  move:   V again: left click a built object to select it, drag an arrow (x red, y green,\n"
                "          z blue) to slide it; right click deselects. V cycles blocks/portal/start/move.\n"
                "  dream:  F6 to the dream and back: build inside the cages; there V gives the cage tool:\n"
                "          click a cage to come back holding its model (object tool: right click places copies),\n"
                "          and the reference tool: click a cage to hold a cage voxel (a live pointer to the cage)\n"
                "  size:   PageUp / PageDown grow / shrink (x2); blocks you place follow your size\n",
                s.worldPath.c_str(), tool.cfg.maxExtent);

    using Clock = std::chrono::steady_clock;
    std::vector<uint32_t> pixels;
    int texW = 0, texH = 0;
    auto last = Clock::now(), titleTime = last;
    double accMs = 0; int accFrames = 0;
    int rc = 0;

    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();
        int fbW, fbH;
        glfwGetFramebufferSize(win, &fbW, &fbH);
        if (fbW == 0 || fbH == 0) { glfwWaitEvents(); last = Clock::now(); continue; }   // minimised

        auto now = Clock::now();
        float dt = std::min(0.1f, std::chrono::duration<float>(now - last).count());
        last = now;
        if (s.walking) walk(win, s, dt);
        else fly(win, s, dt);
        s.view.update(dt);
        {   // what the camera shows: eased during an air-rotation transition
            V3f f, u;
            s.view.visual(f, u);
            s.cam.hasBasis = true;
            s.cam.basisFwd[0] = f.x; s.cam.basisFwd[1] = f.y; s.cam.basisFwd[2] = f.z;
            s.cam.basisUp[0] = u.x;  s.cam.basisUp[1] = u.y;  s.cam.basisUp[2] = u.z;
        }

        int w = std::max(1, (int)(fbW * s.renderScale)), h = std::max(1, (int)(fbH * s.renderScale));
        pixels.resize((size_t)w * h);
        Params p = makeParams(s.cam, w, h, s.settings);

        // Another tool picked in the Tab menu drops the move selection; world
        // changes from input are committed (and uploaded) once per frame.
        if (tool.tool() != BuildTool::TOOL_MOVE && mtool.hasSelection()) dropSelection(s);
        if (s.worldDirty) { commitWorld(s); s.worldDirty = false; }

        // Editor: build target under the reticle and its ghost preview, plus
        // the start box (invisible in play mode).
        Overlay ghosts[kMaxOverlays];
        int nGhosts = 0;
        if (!s.playing) {
            if (s.captured && !s.menuOpen) {
                V3f f, u;
                s.view.visual(f, u);
                float dir[3] = {f.x, f.y, f.z};
                PickHit hit = pickVoxel(s.world->octree(), s.cam.pos, dir, tool.cfg.reach);
                float keep[4] = {s.player->pos.x, s.player->pos.y, s.player->pos.z, s.player->cfg.radius};
                s.hoverCage = -1;
                if (tool.tool() == BuildTool::TOOL_CAGE || tool.tool() == BuildTool::TOOL_REFERENCE) {
                    s.hoverCage = s.levels->dream->pickCage(s.cam.pos, dir, 2.0f * tool.cfg.reach);
                    if (s.hoverCage >= 0) {
                        Overlay& o = ghosts[nGhosts++];
                        s.levels->dream->cageBounds(s.hoverCage, o.lo, o.hi);
                        if (tool.tool() == BuildTool::TOOL_REFERENCE) { o.color[0] = 0.55f; o.color[1] = 0.85f; o.color[2] = 1.0f; }
                        else { o.color[0] = 1.0f; o.color[1] = 0.85f; o.color[2] = 0.3f; }
                        o.color[3] = 0.18f;
                        o.style = OVERLAY_FILL_HOST;
                        Overlay& l = ghosts[nGhosts++];
                        l = o; l.color[3] = 0.0f; l.style = OVERLAY_LINES_HOST | OVERLAY_ONTOP_HOST;
                    }
                } else if (tool.tool() == BuildTool::TOOL_OBJECT) {
                    otool.aim(hit, s.cam.pos, dir, *s.world, s.sizeStep, s.walking ? keep : nullptr,
                              s.inDream ? s.levels->dream.get() : nullptr);
                    otool.fillGhost(p);
                } else if (tool.tool() == BuildTool::TOOL_PORTAL) {
                    ptool.aim(hit, s.cam.pos, dir, *s.portals, tool.level());
                    nGhosts = ptool.ghosts(*s.portals, ghosts, kMaxOverlays - 2);
                } else if (tool.tool() == BuildTool::TOOL_MOVE) {
                    mtool.aim(hit, s.cam.pos, dir);
                    nGhosts = mtool.ghosts(ghosts, kMaxOverlays - 2);
                } else if (tool.tool() == BuildTool::TOOL_START) {
                    stool.aim(hit, s.cam.pos, dir, *s.level, opt.buildLevel, s.player->cfg.radius / s.player->scale());
                    nGhosts = stool.ghosts(*s.level, ghosts, kMaxOverlays - 2);
                } else if (tool.tool() == BuildTool::TOOL_NONE) {
                    tool.aim(PickHit{});                 // nothing aimed, no ghost
                } else {
                    tool.aim(hit, s.cam.pos, dir);
                    nGhosts = tool.ghosts(ghosts, kMaxOverlays - 2);
                }
            } else {
                tool.aim(PickHit{});
            }
            if (!(tool.tool() == BuildTool::TOOL_START && stool.hoveringStart()))
                nGhosts += StartTool::marker(s.level->start, ghosts + nGhosts, kMaxOverlays - nGhosts);
        }
        setOverlays(p, ghosts, nGhosts);
        s.portals->fillParams(p);

        auto t0 = Clock::now();
        if (!renderer.render(p, pixels.data())) { rc = 1; break; }
        accMs += std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
        ++accFrames;

        if (w != texW || h != texH) {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            texW = w; texH = h;
        } else {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        }
        glViewport(0, 0, fbW, fbH);
        glBegin(GL_QUADS);   // row 0 of the image is the top of the screen
        glTexCoord2f(0, 1); glVertex2f(-1, -1);
        glTexCoord2f(1, 1); glVertex2f( 1, -1);
        glTexCoord2f(1, 0); glVertex2f( 1,  1);
        glTexCoord2f(0, 0); glVertex2f(-1,  1);
        glEnd();
        if (s.captured && !s.menuOpen) drawReticle(fbW, fbH);
        if (!s.playing) drawBuildHud(tool, fbW, fbH);
        if (s.menuOpen) {
            float mx, my; cursorFb(win, mx, my);
            drawBuildUi(tool, fbW, fbH, mx, my);
        }
        glfwSwapBuffers(win);

        double since = std::chrono::duration<double>(now - titleTime).count();
        if (since >= 0.5) {
            double frameMs = since * 1000.0 / accFrames;
            char filt[32];
            if (!s.settings.filter) std::snprintf(filt, sizeof filt, "flat");
            else if (s.settings.texels == 0) std::snprintf(filt, sizeof filt, "smooth");
            else std::snprintf(filt, sizeof filt, "%dx%d texels", s.settings.texels, s.settings.texels);
            const Player& player = *s.player;
            const Portals& portals = *s.portals;
            char tools[96];
            char size[24] = "";
            if (s.sizeStep) std::snprintf(size, sizeof size, s.sizeStep > 0 ? " | size x%d" : " | size 1/%d", 1 << std::abs(s.sizeStep));
            if (s.playing) std::snprintf(tools, sizeof tools, "PLAY (F5: editor)");
            else if (tool.tool() == BuildTool::TOOL_CAGE)
                std::snprintf(tools, sizeof tools, "DREAM cage tool%s%s", s.hoverCage >= 0 ? ": click to take a copy" : "", size);
            else if (tool.tool() == BuildTool::TOOL_REFERENCE)
                std::snprintf(tools, sizeof tools, "DREAM reference tool%s%s", s.hoverCage >= 0 ? ": click to take a cage voxel" : "", size);
            else if (tool.tool() == BuildTool::TOOL_OBJECT)
                std::snprintf(tools, sizeof tools, "%s object (%s)%s%s%s", s.inDream ? "DREAM" : "EDIT", otool.held().name.c_str(),
                              otool.canPlace() ? "" : " - ", otool.canPlace() ? "" : otool.why(), size);
            else if (tool.tool() == BuildTool::TOOL_NONE)
                std::snprintf(tools, sizeof tools, "%s (no tool)%s", s.inDream ? "DREAM" : "EDIT", size);
            else std::snprintf(tools, sizeof tools, "%s %s 1/%d%s%s", s.inDream ? "DREAM" : "EDIT",
                               tool.tool() == BuildTool::TOOL_PORTAL ? (portals.linked() ? "portal (linked)" : portals.count() ? "portal (1 door)" : "portal")
                               : tool.tool() == BuildTool::TOOL_START ? (s.level->start.set ? "start (set)" : "start (not set)")
                               : tool.tool() == BuildTool::TOOL_MOVE ? (mtool.dragging() ? "move (dragging)" : mtool.hasSelection() ? "move (selected)" : "move")
                               : tool.inVoid() ? "blocks (void)" : "blocks",
                               1 << tool.sizeStep(), tool.grip() ? " grip" : "", size);
            const bool showNotice = glfwGetTime() < s.noticeUntil;
            char title[512];
            std::snprintf(title, sizeof title,
                          "Octree renderer%s%s%s | %s | %s | %s | %dx%d | %.0f fps (render %.2f ms) | %s %.0f deg | %s | shadows %s | colour %s | LOD %s | %s",
                          showNotice ? " | >> " : "", showNotice ? s.notice.c_str() : "", showNotice ? " <<" : "",
                          s.level->name.c_str(), tools,
                          !s.walking ? "fly" : player.gripping ? "GRIP" : player.grounded ? "walk" : airRotating(s) ? "air: WASD/QE rotate" : "walk (air)",
                          w, h, 1000.0 / frameMs, accMs / accFrames,
                          s.settings.projection == PROJ_FISHEYE_HOST ? "fisheye" : "perspective", activeFov(s),
                          modeName(s.settings.mode),
                          s.settings.shadows ? "on" : "off", filt, s.settings.lodPixels > 0 ? "on" : "off",
                          renderer.name().c_str());
            glfwSetWindowTitle(win, title);
            titleTime = now; accMs = 0; accFrames = 0;
        }
    }

    glDeleteTextures(1, &tex);
    glfwDestroyWindow(win);
    glfwTerminate();
    return rc;
}
