#include "build_ui.hpp"
#include <GLFW/glfw3.h>   // also includes the platform's GL/gl.h
#include <algorithm>
#include <cmath>

namespace {

float uiScale(int fbH) { return std::clamp(fbH / 720.0f, 1.0f, 2.5f); }

void begin2D(int fbW, int fbH) {
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, fbW, fbH, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

void end2D() {
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_BLEND);
    glEnable(GL_TEXTURE_2D);
    glColor4f(1, 1, 1, 1);
}

void rect(float x, float y, float w, float h, float r, float g, float b, float a) {
    glColor4f(r, g, b, a);
    glBegin(GL_QUADS);
    glVertex2f(x, y); glVertex2f(x + w, y); glVertex2f(x + w, y + h); glVertex2f(x, y + h);
    glEnd();
}

void frame(const UiRect& q, float t, float r, float g, float b, float a) {
    rect(q.x - t, q.y - t, q.w + 2 * t, t, r, g, b, a);
    rect(q.x - t, q.y + q.h, q.w + 2 * t, t, r, g, b, a);
    rect(q.x - t, q.y, t, q.h, r, g, b, a);
    rect(q.x + q.w, q.y, t, q.h, r, g, b, a);
}

void colorRect(const UiRect& q, uint32_t c, float inset = 0) {
    rect(q.x + inset, q.y + inset, q.w - 2 * inset, q.h - 2 * inset,
         (c & 255) / 255.0f, ((c >> 8) & 255) / 255.0f, ((c >> 16) & 255) / 255.0f, 1.0f);
}

// Grip icon: diagonal "tread" stripes.
void gripIcon(const UiRect& q, bool on) {
    rect(q.x, q.y, q.w, q.h, on ? 0.20f : 0.16f, on ? 0.40f : 0.18f, on ? 0.95f : 0.22f, 1.0f);
    glColor4f(1, 1, 1, on ? 0.9f : 0.35f);
    glLineWidth(2.0f);
    glBegin(GL_LINES);
    for (int i = 1; i < 4; ++i) {
        float t = q.w * i / 4.0f;
        glVertex2f(q.x + t, q.y + q.h * 0.2f); glVertex2f(q.x + t - q.w * 0.18f, q.y + q.h * 0.8f);
    }
    glEnd();
}

// Tool icons: blocks (a little stack), portal (black door with a blue doorway),
// start (a green box with its facing plate).
void toolIcon(const UiRect& q, int tool, uint32_t c) {
    rect(q.x, q.y, q.w, q.h, 0.16f, 0.17f, 0.20f, 1.0f);
    if (tool == BuildTool::TOOL_BLOCKS) {
        float s = q.w * 0.3f;
        colorRect({q.x + q.w * 0.2f, q.y + q.h * 0.5f, s, s}, c);
        colorRect({q.x + q.w * 0.5f, q.y + q.h * 0.5f, s, s}, c);
        colorRect({q.x + q.w * 0.35f, q.y + q.h * 0.2f, s, s}, c);
    } else if (tool == BuildTool::TOOL_CAGE) {   // a cage: a wire cube
        UiRect b{q.x + q.w * 0.22f, q.y + q.h * 0.22f, q.w * 0.56f, q.h * 0.56f};
        const float t = std::max(1.5f, q.w * 0.05f);
        frame(b, t, 0.96f, 0.94f, 1.0f, 1.0f);
        UiRect back{b.x + b.w * 0.25f, b.y - b.h * 0.18f, b.w, b.h};
        frame(back, t * 0.7f, 0.75f, 0.72f, 0.9f, 0.8f);
    } else if (tool == BuildTool::TOOL_NONE) {        // an empty hand: a ring with a slash
        const float cx = q.x + q.w * 0.5f, cy = q.y + q.h * 0.5f, r = q.w * 0.30f, t = std::max(2.0f, q.w * 0.07f);
        for (int i = 0; i < 24; ++i) {               // the ring, as small squares
            float a = i * 6.2831853f / 24;
            rect(cx + r * std::cos(a) - t / 2, cy + r * std::sin(a) - t / 2, t, t, 0.85f, 0.85f, 0.88f, 1.0f);
        }
        for (int i = -5; i <= 5; ++i) {              // the slash
            float u = i / 5.0f * r * 0.72f;
            rect(cx + u - t / 2, cy - u - t / 2, t, t, 0.85f, 0.85f, 0.88f, 1.0f);
        }
    } else if (tool == BuildTool::TOOL_REFERENCE) {   // a cage with a link: a small cube tied to a wire cube
        UiRect b{q.x + q.w * 0.40f, q.y + q.h * 0.18f, q.w * 0.44f, q.h * 0.44f};
        const float t = std::max(1.5f, q.w * 0.05f);
        frame(b, t, 0.96f, 0.94f, 1.0f, 1.0f);
        rect(q.x + q.w * 0.16f, q.y + q.h * 0.58f, q.w * 0.24f, q.h * 0.24f, 0.55f, 0.85f, 1.0f, 1.0f);
        rect(q.x + q.w * 0.34f, q.y + q.h * 0.50f, q.w * 0.12f, t, 0.55f, 0.85f, 1.0f, 1.0f);
    } else if (tool == BuildTool::TOOL_OBJECT) { // the held object: a few blocks in a hand-ish bracket
        float s = q.w * 0.22f;
        colorRect({q.x + q.w * 0.28f, q.y + q.h * 0.30f, s, s}, c);
        colorRect({q.x + q.w * 0.50f, q.y + q.h * 0.30f, s, s}, c);
        colorRect({q.x + q.w * 0.39f, q.y + q.h * 0.12f + s * 0.0f, s, s}, 0xFFE0C090u);
        rect(q.x + q.w * 0.18f, q.y + q.h * 0.62f, q.w * 0.64f, q.h * 0.12f, 0.95f, 0.85f, 0.4f, 1.0f);
    } else if (tool == BuildTool::TOOL_MOVE) {   // four arrows (a cross with tips)
        const float cx = q.x + q.w * 0.5f, cy = q.y + q.h * 0.5f, L = q.w * 0.34f, t = std::max(2.0f, q.w * 0.08f), k = q.w * 0.14f;
        rect(cx - L, cy - t / 2, 2 * L, t, 0.95f, 0.85f, 0.4f, 1.0f);
        rect(cx - t / 2, cy - L, t, 2 * L, 0.95f, 0.85f, 0.4f, 1.0f);
        rect(cx + L - k / 2, cy - k / 2, k, k, 1.0f, 0.3f, 0.25f, 1.0f);
        rect(cx - k / 2, cy - L - k / 2, k, k, 0.3f, 0.95f, 0.35f, 1.0f);
        rect(cx - L - k / 2, cy - k / 2, k, k, 0.95f, 0.85f, 0.4f, 1.0f);
        rect(cx - k / 2, cy + L - k / 2, k, k, 0.95f, 0.85f, 0.4f, 1.0f);
    } else if (tool == BuildTool::TOOL_START) {
        UiRect b{q.x + q.w * 0.22f, q.y + q.h * 0.22f, q.w * 0.56f, q.h * 0.56f};
        rect(b.x, b.y, b.w, b.h, 0.35f, 1.0f, 0.45f, 0.3f);
        frame(b, std::max(1.5f, q.w * 0.05f), 0.35f, 1.0f, 0.45f, 1.0f);
        rect(b.x + b.w * 0.3f, b.y + b.h * 0.3f, b.w * 0.4f, b.h * 0.25f, 0.35f, 1.0f, 0.45f, 1.0f);
    } else {
        rect(q.x + q.w * 0.25f, q.y + q.h * 0.12f, q.w * 0.5f, q.h * 0.76f, 0.03f, 0.03f, 0.04f, 1.0f);
        rect(q.x + q.w * 0.31f, q.y + q.h * 0.2f, q.w * 0.38f, q.h * 0.62f, 0.25f, 0.55f, 1.0f, 1.0f);
    }
}

// Size icon: a square whose side shows the block size (1, 1/2, 1/4...).
void sizeIcon(const UiRect& q, int step, uint32_t c) {
    rect(q.x, q.y, q.w, q.h, 0.16f, 0.17f, 0.20f, 1.0f);
    float s = std::max(3.0f, q.w * 0.8f / (float)(1 << step));
    UiRect in{q.x + (q.w - s) * 0.5f, q.y + (q.h - s) * 0.5f, s, s};
    colorRect(in, c);
}

} // namespace

BuildUiLayout buildUiLayout(const BuildTool& tool, int fbW, int fbH) {
    const float k = uiScale(fbH), cell = 40 * k, gap = 8 * k, pad = 18 * k;
    const int cols = 8;
    const int n = (int)tool.palette().size(), rows = (n + cols - 1) / cols;
    const float gridW = cols * cell + (cols - 1) * gap;
    const float panelW = gridW + 2 * pad;
    const float panelH = pad + (cell + gap * 3) + rows * (cell + gap) + gap * 2 + cell + pad;
    BuildUiLayout L;
    L.panel = {(fbW - panelW) * 0.5f, (fbH - panelH) * 0.5f, panelW, panelH};
    float x0 = L.panel.x + pad, y0 = L.panel.y + pad;
    for (int t = 0; t < BuildTool::TOOL_COUNT; ++t) {
        if (!tool.available((BuildTool::Tool)t)) continue;
        L.tools.push_back({x0 + (float)L.toolIds.size() * (cell + gap), y0, cell, cell});
        L.toolIds.push_back(t);
    }
    y0 += cell + gap * 3;
    for (int i = 0; i < n; ++i)
        L.swatches.push_back({x0 + (i % cols) * (cell + gap), y0 + (i / cols) * (cell + gap), cell, cell});
    float yb = y0 + rows * (cell + gap) + gap * 2;
    L.grip = {x0, yb, cell, cell};
    for (int s = 0; s < tool.cfg.sizeSteps; ++s)
        L.sizes.push_back({x0 + (s + 2) * (cell + gap), yb, cell, cell});
    return L;
}

bool buildUiClick(BuildTool& tool, int fbW, int fbH, float x, float y) {
    BuildUiLayout L = buildUiLayout(tool, fbW, fbH);
    for (size_t i = 0; i < L.tools.size(); ++i)
        if (L.tools[i].contains(x, y)) { tool.setTool((BuildTool::Tool)L.toolIds[i]); return true; }
    for (size_t i = 0; i < L.swatches.size(); ++i)
        if (L.swatches[i].contains(x, y)) { tool.select((int)i); return true; }
    if (L.grip.contains(x, y)) { tool.setGrip(!tool.grip()); return true; }
    for (size_t i = 0; i < L.sizes.size(); ++i)
        if (L.sizes[i].contains(x, y)) { tool.setSizeStep((int)i); return true; }
    return L.panel.contains(x, y);
}

void drawBuildUi(const BuildTool& tool, int fbW, int fbH, float mx, float my) {
    BuildUiLayout L = buildUiLayout(tool, fbW, fbH);
    const float k = uiScale(fbH);
    begin2D(fbW, fbH);
    rect(0, 0, (float)fbW, (float)fbH, 0, 0, 0, 0.35f);                       // dim the world
    rect(L.panel.x, L.panel.y, L.panel.w, L.panel.h, 0.09f, 0.10f, 0.12f, 0.94f);
    frame(L.panel, 1 * k, 1, 1, 1, 0.12f);

    for (size_t t = 0; t < L.tools.size(); ++t) {
        toolIcon(L.tools[t], L.toolIds[t], tool.palette()[tool.selected()]);
        if (L.toolIds[t] == tool.tool()) frame(L.tools[t], 3 * k, 1, 1, 1, 1);
        else if (L.tools[t].contains(mx, my)) frame(L.tools[t], 2 * k, 1, 1, 1, 0.5f);
    }
    for (size_t i = 0; i < L.swatches.size(); ++i) {
        colorRect(L.swatches[i], tool.palette()[i]);
        if ((int)i == tool.selected()) frame(L.swatches[i], 3 * k, 1, 1, 1, 1);
        else if (L.swatches[i].contains(mx, my)) frame(L.swatches[i], 2 * k, 1, 1, 1, 0.5f);
    }
    gripIcon(L.grip, tool.grip());
    if (tool.grip()) frame(L.grip, 3 * k, 1, 1, 1, 1);
    else if (L.grip.contains(mx, my)) frame(L.grip, 2 * k, 1, 1, 1, 0.5f);
    for (size_t s = 0; s < L.sizes.size(); ++s) {
        sizeIcon(L.sizes[s], (int)s, tool.palette()[tool.selected()]);
        if ((int)s == tool.sizeStep()) frame(L.sizes[s], 3 * k, 1, 1, 1, 1);
        else if (L.sizes[s].contains(mx, my)) frame(L.sizes[s], 2 * k, 1, 1, 1, 0.5f);
    }
    end2D();
}

void drawReticle(int fbW, int fbH) {
    const float k = uiScale(fbH);
    begin2D(fbW, fbH);
    // A centre dot and four ticks with a gap, each with a dark outline so it
    // reads on bright and dark backgrounds alike.
    const float cx = std::floor(fbW * 0.5f), cy = std::floor(fbH * 0.5f);
    const float d = 2 * k, gap = 5 * k, len = 7 * k, t = 2 * k, o = std::max(1.0f, k);
    auto bar = [&](float x, float y, float w, float h) {
        rect(x - o, y - o, w + 2 * o, h + 2 * o, 0, 0, 0, 0.55f);
        rect(x, y, w, h, 1, 1, 1, 0.95f);
    };
    bar(cx - d / 2, cy - d / 2, d, d);                       // dot
    bar(cx - gap - len, cy - t / 2, len, t);                 // left
    bar(cx + gap, cy - t / 2, len, t);                       // right
    bar(cx - t / 2, cy - gap - len, t, len);                 // top
    bar(cx - t / 2, cy + gap, t, len);                       // bottom
    end2D();
}

void drawBuildHud(const BuildTool& tool, int fbW, int fbH) {
    const float k = uiScale(fbH);
    begin2D(fbW, fbH);
    // Selected block: colour swatch, size icon, grip icon.
    const float cell = 34 * k, m = 14 * k, g = 6 * k;
    UiRect sw{m, fbH - m - cell, cell, cell};
    UiRect sz{m + cell + g, fbH - m - cell, cell, cell};
    UiRect gr{m + 2 * (cell + g), fbH - m - cell, cell, cell};
    UiRect tl{m + 3 * (cell + g), fbH - m - cell, cell, cell};
    rect(m - g, fbH - m - cell - g, 4 * cell + 5 * g, cell + 2 * g, 0, 0, 0, 0.45f);
    toolIcon(tl, tool.tool(), tool.palette()[tool.selected()]);
    colorRect(sw, tool.palette()[tool.selected()]);
    sizeIcon(sz, tool.sizeStep(), tool.palette()[tool.selected()]);
    gripIcon(gr, tool.grip());
    end2D();
}
