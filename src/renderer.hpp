#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "octree.hpp"

// Host copies of `Overlay` / `Params` in kernels/octree.cl. renderer.cpp
// static_asserts that the layouts match, so edit them together.
constexpr int kMaxOverlays = 12;
enum OverlayStyle { OVERLAY_FILL_HOST = 0, OVERLAY_LINES_HOST = 1, OVERLAY_ONTOP_HOST = 2 };   // ONTOP: bit, combine with FILL/LINES
struct Overlay {
    float lo[3], hi[3];     // box, octree space
    float color[4];         // rgb (display space) + fill opacity
    int   style;
};

struct PortalFrame {
    float c[3], r[3], u[3], n[3];   // centre, right, up, entrance-side normal (octree space)
    float halfW, halfH;
};

struct Params {
    float camPos[3];
    float camFwd[3];
    float camRight[3];
    float camUp[3];
    float sunDir[3];
    float tanHalfFov;
    float aspect;
    float lodCoef;
    int   width;
    int   height;
    int   mode;
    int   shadows;
    int   maxIters;
    int   projection;
    float halfFov;
    int   filter;
    int   texels;
    float pixelAngle;
    int   overlayCount;
    Overlay overlays[kMaxOverlays];
    int   portalOpen;
    int   portalMaxHops;
    PortalFrame portalIn, portalOut;
    int   ghostOn;            // see setGhost(): the held model, drawn see-through
    float ghostOrigin[3];     // octree-space corner of the ghost's root cube
    float ghostInvSize;       // 1 / its edge length
};

// Copies up to kMaxOverlays boxes into p (drawn on top of the scene, depth-correct).
void setOverlays(Params& p, const Overlay* list, int count);

// Ray pick against the world, with the exact same traversal as the GPU (no LOD).
struct PickHit {
    bool hit = false;
    float t = 0;            // distance along the (normalised) ray
    float pos[3] = {0, 0, 0};   // hit point, octree space
    int normal[3] = {0, 0, 0};  // face normal (axis-aligned, pointing out of the voxel)
    float size = 0;         // edge of the voxel that was hit
};
PickHit pickVoxel(const Octree& oct, const float origin[3], const float dir[3], float maxDist);

enum Projection { PROJ_PERSPECTIVE_HOST = 0, PROJ_FISHEYE_HOST = 1 };

enum RenderMode { RENDER_SHADED = 0, RENDER_NORMALS, RENDER_STEPS, RENDER_DEPTH, RENDER_MODE_COUNT };

// Free-look camera in octree space (the scene occupies [1,2]^3).
struct Camera {
    float pos[3] = {0, 0, 0};
    float yaw = 0, pitch = 0;          // radians; yaw 0 looks toward +x
    // Optional full orientation (overrides yaw/pitch, allows roll / upside down).
    bool hasBasis = false;
    float basisFwd[3] = {1, 0, 0}, basisUp[3] = {0, 1, 0};
    void forward(float out[3]) const;
    void right(float out[3]) const;    // horizontal, ignores pitch
    void lookAt(const float target[3]);
};

struct RenderSettings {
    int mode = RENDER_SHADED;
    bool shadows = true;
    bool filter = true;                // bilinear colour filtering across voxels
    int texels = 4;                    // texels per voxel face, nearest sampled; 0 = smooth
    float lodPixels = 1.0f;            // stop descending below this many pixels; 0 = off
    int projection = PROJ_PERSPECTIVE_HOST;   // fisheye available with P / --lens fisheye
    float fovDeg = 70.0f;              // perspective: vertical field of view
    float fisheyeFovDeg = 200.0f;      // fisheye: horizontal field of view (up to 360)
};

Params makeParams(const Camera& cam, int width, int height, const RenderSettings& s);
// Orbit position looking at the sphere; distance is from the scene centre.
Camera defaultCamera(float orbitAngleDeg = 35.0f, float distance = 1.25f);

// A backend renders one frame into an RGBA8 pixel buffer (row 0 = top).
class Renderer {
public:
    virtual ~Renderer() = default;
    virtual bool render(const Params& p, uint32_t* pixels) = 0;
    virtual std::string name() const = 0;
    // The octree changed (edits): upload it again. The object must be the one
    // the renderer was created with (it may have been rewritten in place).
    virtual bool updateOctree(const Octree& oct) = 0;
    // A second octree drawn as a see-through ghost where Params::ghostOn is
    // set (placed by ghostOrigin / ghostInvSize). nullptr: none. Like the
    // main octree, it is read in place / uploaded on each call.
    virtual bool setGhost(const Octree* ghost) = 0;
};

// OpenCL backend. Returns nullptr (with a message on stderr) if unavailable.
// device < 0 picks the first GPU.
std::unique_ptr<Renderer> createOpenCLRenderer(const Octree& oct, const std::string& kernelSource, int device);

// Runs the same kernel source, compiled as C++, on all CPU cores.
std::unique_ptr<Renderer> createCpuRenderer(const Octree& oct);
