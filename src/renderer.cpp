#include "renderer.hpp"
#include "cl_loader.hpp"
#include "cl_shim.hpp"   // the kernel compiled as C++ (only in this file)

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <thread>

thread_local size_t clshim::g_global_id[2];

static_assert(sizeof(Params) == sizeof(clshim::Params), "Params differs from kernels/octree.cl");
static_assert(offsetof(Params, lodCoef) == offsetof(clshim::Params, lodCoef), "Params differs from kernels/octree.cl");
static_assert(offsetof(Params, maxIters) == offsetof(clshim::Params, maxIters), "Params differs from kernels/octree.cl");
static_assert(offsetof(Params, halfFov) == offsetof(clshim::Params, halfFov), "Params differs from kernels/octree.cl");
static_assert(offsetof(Params, filter) == offsetof(clshim::Params, filter), "Params differs from kernels/octree.cl");
static_assert(offsetof(Params, texels) == offsetof(clshim::Params, texels), "Params differs from kernels/octree.cl");
static_assert(offsetof(Params, overlays) == offsetof(clshim::Params, overlays), "Params differs from kernels/octree.cl");
static_assert(sizeof(Overlay) == sizeof(clshim::Overlay), "Overlay differs from kernels/octree.cl");
static_assert(kMaxOverlays == MAX_OVERLAYS, "MAX_OVERLAYS differs from kernels/octree.cl");
static_assert(offsetof(Params, portalIn) == offsetof(clshim::Params, portalIn), "Params differs from kernels/octree.cl");
static_assert(sizeof(PortalFrame) == sizeof(clshim::PortalFrame), "PortalFrame differs from kernels/octree.cl");
static_assert(offsetof(Params, ghostOrigin) == offsetof(clshim::Params, ghostOrigin), "Params differs from kernels/octree.cl");

// ---------------------------------------------------------------------------
// Camera
// ---------------------------------------------------------------------------
static void norm3(float* v) {
    float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    v[0] /= l; v[1] /= l; v[2] /= l;
}
static void cross3(const float* a, const float* b, float* r) {
    r[0] = a[1] * b[2] - a[2] * b[1];
    r[1] = a[2] * b[0] - a[0] * b[2];
    r[2] = a[0] * b[1] - a[1] * b[0];
}

void Camera::forward(float o[3]) const {
    o[0] = std::cos(pitch) * std::cos(yaw);
    o[1] = std::sin(pitch);
    o[2] = std::cos(pitch) * std::sin(yaw);
}
void Camera::right(float o[3]) const {
    // forward x worldUp, flattened.
    o[0] = -std::sin(yaw);
    o[1] = 0.0f;
    o[2] = std::cos(yaw);
}
void Camera::lookAt(const float t[3]) {
    float d[3] = {t[0] - pos[0], t[1] - pos[1], t[2] - pos[2]};
    norm3(d);
    yaw = std::atan2(d[2], d[0]);
    pitch = std::asin(std::clamp(d[1], -1.0f, 1.0f));
}

Camera defaultCamera(float orbitAngleDeg, float dist) {
    Camera c;
    float a = orbitAngleDeg * 3.14159265f / 180.0f;
    float target[3] = {1.5f, 1.33f, 1.5f};
    c.pos[0] = target[0] + std::cos(a) * dist;
    c.pos[1] = target[1] + 0.36f * dist;
    c.pos[2] = target[2] + std::sin(a) * dist;
    c.lookAt(target);
    return c;
}

Params makeParams(const Camera& cam, int width, int height, const RenderSettings& s) {
    Params p{};
    std::memcpy(p.camPos, cam.pos, sizeof p.camPos);
    float worldUp[3] = {0, 1, 0};
    if (cam.hasBasis) {
        std::memcpy(p.camFwd, cam.basisFwd, sizeof p.camFwd);
        norm3(p.camFwd);
        cross3(p.camFwd, cam.basisUp, p.camRight); norm3(p.camRight);
    } else {
        cam.forward(p.camFwd);
        cross3(p.camFwd, worldUp, p.camRight); norm3(p.camRight);
    }
    cross3(p.camRight, p.camFwd, p.camUp);
    float sun[3] = {-0.45f, 0.75f, -0.35f}; norm3(sun);
    std::memcpy(p.sunDir, sun, sizeof sun);
    p.tanHalfFov = std::tan(s.fovDeg * 0.5f * 3.14159265f / 180.0f);
    p.aspect = (float)width / (float)height;
    p.projection = s.projection;
    p.filter = s.filter ? 1 : 0;
    p.texels = s.texels;
    p.halfFov = s.fisheyeFovDeg * 0.5f * 3.14159265f / 180.0f;
    // Angular size of one pixel (rays are normalised, so t = distance).
    p.pixelAngle = s.projection == PROJ_FISHEYE_HOST ? 2.0f * p.halfFov / (float)width
                                                      : 2.0f * p.tanHalfFov / (float)height;
    p.lodCoef = s.lodPixels * p.pixelAngle;
    p.overlayCount = 0;
    p.portalOpen = 0;
    p.portalMaxHops = 4;
    p.width = width; p.height = height;
    p.mode = s.mode; p.shadows = s.shadows ? 1 : 0;
    p.maxIters = 4096;
    return p;
}

void setOverlays(Params& p, const Overlay* list, int count) {
    p.overlayCount = std::clamp(count, 0, kMaxOverlays);
    for (int i = 0; i < p.overlayCount; ++i) p.overlays[i] = list[i];
}

PickHit pickVoxel(const Octree& oct, const float o[3], const float d[3], float maxDist) {
    PickHit r;
    if (oct.nodes.size() < 2) return r;
    clshim::float3 org{o[0], o[1], o[2]};
    clshim::float3 dir = clshim::normalize(clshim::float3{d[0], d[1], d[2]});
    clshim::Hit h = clshim::castRay(oct.nodes.data(), org, dir, maxDist, 0.0f, 4096, 0.0f);
    if (h.t < 0.0f) return r;
    r.hit = true;
    r.t = h.t;
    r.size = h.size;
    float dv[3] = {dir.x, dir.y, dir.z};
    for (int a = 0; a < 3; ++a) r.pos[a] = o[a] + dv[a] * h.t;
    r.normal[h.axis] = dv[h.axis] > 0.0f ? -1 : 1;
    return r;
}

// ---------------------------------------------------------------------------
// CPU backend
// ---------------------------------------------------------------------------
namespace {

class CpuRenderer : public Renderer {
public:
    explicit CpuRenderer(const Octree& o) : oct(&o) {}
    // Reads the octree in place (no copy): just follow the one we are given.
    bool updateOctree(const Octree& o) override { oct = &o; return true; }
    bool setGhost(const Octree* g) override { ghost = g; return true; }
    std::string name() const override {
        return "CPU (" + std::to_string(std::max(1u, std::thread::hardware_concurrency())) + " threads)";
    }
    bool render(const Params& hp, uint32_t* out) override {
        clshim::Params p;
        std::memcpy(&p, &hp, sizeof p);
        unsigned nt = std::max(1u, std::thread::hardware_concurrency());
        std::vector<std::thread> pool;
        for (unsigned t = 0; t < nt; ++t) {
            pool.emplace_back([&, t] {
                for (int y = (int)t; y < p.height; y += (int)nt)
                    for (int x = 0; x < p.width; ++x) {
                        clshim::g_global_id[0] = (size_t)x;
                        clshim::g_global_id[1] = (size_t)y;
                        clshim::render(oct->nodes.data(), oct->colors.data(), out, &p,
                                       ghost ? ghost->nodes.data() : kNoGhost, ghost ? ghost->colors.data() : kNoGhost);
                    }
            });
        }
        for (auto& th : pool) th.join();
        return true;
    }
private:
    const Octree* oct;
    const Octree* ghost = nullptr;
    static constexpr uint32_t kNoGhost[2] = {0, 0};
};

// ---------------------------------------------------------------------------
// OpenCL backend
// ---------------------------------------------------------------------------
#define CL_CHECK(expr) do { cl_int e_ = (expr); if (e_ != CL_SUCCESS) { \
    std::fprintf(stderr, "OpenCL error %d at %s:%d: %s\n", e_, __FILE__, __LINE__, #expr); return false; } } while (0)

struct ClDevice { cl_platform_id platform; cl_device_id device; std::string name; cl_device_type type; };

std::vector<ClDevice> listDevices() {
    std::vector<ClDevice> out;
    cl_uint np = 0;
    if (cl.clGetPlatformIDs(0, nullptr, &np) != CL_SUCCESS || np == 0) return out;
    std::vector<cl_platform_id> plats(np);
    cl.clGetPlatformIDs(np, plats.data(), nullptr);
    for (auto pl : plats) {
        cl_uint nd = 0;
        if (cl.clGetDeviceIDs(pl, CL_DEVICE_TYPE_ALL, 0, nullptr, &nd) != CL_SUCCESS || nd == 0) continue;
        std::vector<cl_device_id> devs(nd);
        cl.clGetDeviceIDs(pl, CL_DEVICE_TYPE_ALL, nd, devs.data(), nullptr);
        for (auto d : devs) {
            char name[256] = {0};
            cl_device_type type = 0;
            cl.clGetDeviceInfo(d, CL_DEVICE_NAME, sizeof name - 1, name, nullptr);
            cl.clGetDeviceInfo(d, CL_DEVICE_TYPE, sizeof type, &type, nullptr);
            out.push_back({pl, d, name, type});
        }
    }
    return out;
}

class OpenCLRenderer : public Renderer {
public:
    ~OpenCLRenderer() override {
        if (bOut)    cl.clReleaseMemObject(bOut);
        if (bParams) cl.clReleaseMemObject(bParams);
        if (bColors) cl.clReleaseMemObject(bColors);
        if (bGhostNodes)  cl.clReleaseMemObject(bGhostNodes);
        if (bGhostColors) cl.clReleaseMemObject(bGhostColors);
        if (bNodes)  cl.clReleaseMemObject(bNodes);
        if (kernel)  cl.clReleaseKernel(kernel);
        if (prog)    cl.clReleaseProgram(prog);
        if (queue)   cl.clReleaseCommandQueue(queue);
        if (ctx)     cl.clReleaseContext(ctx);
    }
    std::string name() const override { return deviceName; }

    bool init(const Octree& oct, const std::string& src, int pick) {
        auto devs = listDevices();
        if (devs.empty()) { std::fprintf(stderr, "No OpenCL devices found.\n"); return false; }
        if (pick < 0) {   // prefer the first GPU
            pick = 0;
            for (size_t i = 0; i < devs.size(); ++i)
                if (devs[i].type & CL_DEVICE_TYPE_GPU) { pick = (int)i; break; }
        }
        if (pick >= (int)devs.size()) { std::fprintf(stderr, "OpenCL device %d does not exist.\n", pick); return false; }
        std::printf("OpenCL devices:\n");
        for (size_t i = 0; i < devs.size(); ++i)
            std::printf("  [%zu] %s%s\n", i, devs[i].name.c_str(), (int)i == pick ? "   <- using" : "");
        const ClDevice& D = devs[pick];
        device = D.device;
        deviceName = D.name;

        cl_int err = 0;
        cl_context_properties props[] = {CL_CONTEXT_PLATFORM, (cl_context_properties)D.platform, 0};
        ctx = cl.clCreateContext(props, 1, &device, nullptr, nullptr, &err); CL_CHECK(err);
        queue = cl.clCreateCommandQueue(ctx, device, 0, &err); CL_CHECK(err);

        const char* s = src.c_str(); size_t len = src.size();
        prog = cl.clCreateProgramWithSource(ctx, 1, &s, &len, &err); CL_CHECK(err);
        err = cl.clBuildProgram(prog, 1, &device, "-cl-mad-enable", nullptr, nullptr);
        if (err != CL_SUCCESS) {
            size_t logLen = 0;
            cl.clGetProgramBuildInfo(prog, device, CL_PROGRAM_BUILD_LOG, 0, nullptr, &logLen);
            std::string log(logLen, '\0');
            cl.clGetProgramBuildInfo(prog, device, CL_PROGRAM_BUILD_LOG, logLen, &log[0], nullptr);
            std::fprintf(stderr, "Kernel build failed:\n%s\n", log.c_str());
            return false;
        }
        kernel = cl.clCreateKernel(prog, "render", &err); CL_CHECK(err);

        if (!updateOctree(oct)) return false;
        bParams = cl.clCreateBuffer(ctx, CL_MEM_READ_ONLY, sizeof(Params), nullptr, &err); CL_CHECK(err);
        CL_CHECK(cl.clSetKernelArg(kernel, 3, sizeof(cl_mem), &bParams));
        return setGhost(nullptr);
    }

    // The ghost's buffers (a tiny placeholder when there is none: the kernel
    // arguments must always be bound).
    bool setGhost(const Octree* g) override {
        static const uint32_t none[2] = {0, 0};
        const uint32_t* nd = g ? g->nodes.data() : none;
        const uint32_t* cd = g ? g->colors.data() : none;
        size_t nb = g ? g->nodes.size() * 4 : 8, cb = g ? g->colors.size() * 4 : 8;
        cl_int err = 0;
        if (nb > gNodeCap || cb > gColorCap || !bGhostNodes) {
            if (bGhostNodes) cl.clReleaseMemObject(bGhostNodes);
            if (bGhostColors) cl.clReleaseMemObject(bGhostColors);
            gNodeCap = std::max<size_t>(nb, 4096); gColorCap = std::max<size_t>(cb, 4096);
            bGhostNodes = cl.clCreateBuffer(ctx, CL_MEM_READ_ONLY, gNodeCap, nullptr, &err); CL_CHECK(err);
            bGhostColors = cl.clCreateBuffer(ctx, CL_MEM_READ_ONLY, gColorCap, nullptr, &err); CL_CHECK(err);
            CL_CHECK(cl.clSetKernelArg(kernel, 4, sizeof(cl_mem), &bGhostNodes));
            CL_CHECK(cl.clSetKernelArg(kernel, 5, sizeof(cl_mem), &bGhostColors));
        }
        CL_CHECK(cl.clEnqueueWriteBuffer(queue, bGhostNodes, CL_TRUE, 0, nb, nd, 0, nullptr, nullptr));
        CL_CHECK(cl.clEnqueueWriteBuffer(queue, bGhostColors, CL_TRUE, 0, cb, cd, 0, nullptr, nullptr));
        return true;
    }

    // (Re)creates the node/colour buffers. Buffers are kept with some spare
    // room, so most edits only rewrite them instead of reallocating.
    bool updateOctree(const Octree& oct) override {
        cl_int err = 0;
        size_t nb = oct.nodes.size() * 4, cb = oct.colors.size() * 4;
        if (nb > nodeCap || cb > colorCap) {
            if (bNodes) cl.clReleaseMemObject(bNodes);
            if (bColors) cl.clReleaseMemObject(bColors);
            nodeCap = nb + nb / 4 + 4096; colorCap = cb + cb / 4 + 4096;
            bNodes = cl.clCreateBuffer(ctx, CL_MEM_READ_ONLY, nodeCap, nullptr, &err); CL_CHECK(err);
            bColors = cl.clCreateBuffer(ctx, CL_MEM_READ_ONLY, colorCap, nullptr, &err); CL_CHECK(err);
            CL_CHECK(cl.clSetKernelArg(kernel, 0, sizeof(cl_mem), &bNodes));
            CL_CHECK(cl.clSetKernelArg(kernel, 1, sizeof(cl_mem), &bColors));
        }
        CL_CHECK(cl.clEnqueueWriteBuffer(queue, bNodes, CL_TRUE, 0, nb, oct.nodes.data(), 0, nullptr, nullptr));
        CL_CHECK(cl.clEnqueueWriteBuffer(queue, bColors, CL_TRUE, 0, cb, oct.colors.data(), 0, nullptr, nullptr));
        return true;
    }

    bool render(const Params& p, uint32_t* pixels) override {
        size_t bytes = (size_t)p.width * p.height * 4;
        if (bytes != outBytes) {   // (re)allocate on resize
            if (bOut) cl.clReleaseMemObject(bOut);
            cl_int err = 0;
            bOut = cl.clCreateBuffer(ctx, CL_MEM_WRITE_ONLY, bytes, nullptr, &err); CL_CHECK(err);
            CL_CHECK(cl.clSetKernelArg(kernel, 2, sizeof(cl_mem), &bOut));
            outBytes = bytes;
        }
        const size_t local[2] = {8, 8};
        const size_t global[2] = {(size_t)(p.width + 7) / 8 * 8, (size_t)(p.height + 7) / 8 * 8};
        CL_CHECK(cl.clEnqueueWriteBuffer(queue, bParams, CL_FALSE, 0, sizeof p, &p, 0, nullptr, nullptr));
        CL_CHECK(cl.clEnqueueNDRangeKernel(queue, kernel, 2, nullptr, global, local, 0, nullptr, nullptr));
        CL_CHECK(cl.clEnqueueReadBuffer(queue, bOut, CL_TRUE, 0, bytes, pixels, 0, nullptr, nullptr));
        return true;
    }

private:
    cl_device_id device = nullptr;
    std::string deviceName;
    cl_context ctx = nullptr;
    cl_command_queue queue = nullptr;
    cl_program prog = nullptr;
    cl_kernel kernel = nullptr;
    cl_mem bNodes = nullptr, bColors = nullptr, bParams = nullptr, bOut = nullptr;
    cl_mem bGhostNodes = nullptr, bGhostColors = nullptr;
    size_t gNodeCap = 0, gColorCap = 0;
    size_t nodeCap = 0, colorCap = 0;
    size_t outBytes = 0;
};

} // namespace

std::unique_ptr<Renderer> createCpuRenderer(const Octree& oct) {
    return std::make_unique<CpuRenderer>(oct);
}

std::unique_ptr<Renderer> createOpenCLRenderer(const Octree& oct, const std::string& src, int device) {
    const char* err = nullptr;
    if (!cl.load(&err)) { std::fprintf(stderr, "%s\n", err); return nullptr; }
    auto r = std::make_unique<OpenCLRenderer>();
    if (!r->init(oct, src, device)) return nullptr;
    return r;
}
