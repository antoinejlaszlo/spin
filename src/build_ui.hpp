#pragma once
// Build menu and HUD, drawn with plain OpenGL 1.1 on top of the rendered
// frame. Layout and hit-testing share one function, so what you click is
// exactly what is drawn. Coordinates are framebuffer pixels, origin top-left.
#include "build_tool.hpp"

struct UiRect { float x, y, w, h; bool contains(float px, float py) const {
    return px >= x && px < x + w && py >= y && py < y + h; } };

struct BuildUiLayout {
    UiRect panel;
    std::vector<int> toolIds;          // the available tools, in the order shown
    std::vector<UiRect> tools;      // one per BuildTool::Tool
    std::vector<UiRect> swatches;   // one per palette colour
    UiRect grip;                    // grip on/off toggle
    std::vector<UiRect> sizes;      // one per block size step
};
BuildUiLayout buildUiLayout(const BuildTool& tool, int fbW, int fbH);

// Applies a click at (x, y) to the tool. Returns true if it hit a control.
bool buildUiClick(BuildTool& tool, int fbW, int fbH, float x, float y);

void drawBuildUi(const BuildTool& tool, int fbW, int fbH, float mouseX, float mouseY);
// The selected tool and block (colour, size, grip) in the bottom-left corner.
void drawBuildHud(const BuildTool& tool, int fbW, int fbH);
// The reticle at the centre of the screen (editor and play mode).
void drawReticle(int fbW, int fbH);
