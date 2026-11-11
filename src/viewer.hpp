#pragma once
#include "octree.hpp"
#include "renderer.hpp"
#include <string>
#include "level.hpp"

struct ViewerOptions {
    int width = 1280, height = 720;   // initial window size (resizable)
    float renderScale = 1.0f;         // internal resolution relative to the window
    bool vsync = false;
    bool fullscreen = false;          // start in full screen (F11 toggles)
    bool startFlying = false;         // start in free-fly (noclip) instead of walking
    int buildLevel = 7;               // octree level of the default block size
    bool keyLog = false;              // print every key event to the console (diagnostics)
    std::string worldPath = "worlds/default.world";   // Ctrl+S saves the levels here, Ctrl+O reloads it
    RenderSettings settings;
};

// Opens a window and renders interactively until it is closed: the level
// editor (edit mode) and the game (play mode, F5). The current level of
// `levels` is edited and played; the renderer must have been created with
// its world's octree.
int runViewer(Renderer& renderer, LevelSet& levels, const ViewerOptions& opt);
