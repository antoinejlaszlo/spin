// ============================================================================
//  Sparse voxel octree ray caster
//
//  Traversal: Laine & Karras, "Efficient Sparse Voxel Octrees" (ESVO, 2010).
//  This is the stack-based parametric octree walk (Revelles-style "octree
//  marching") with ESVO's tricks:
//    * the octree lives in [1,2]^3 so every cube corner is an exact float and
//      POP can find the parent scale from the highest differing mantissa bit;
//    * the ray is mirrored so it always travels in -x,-y,-z, which removes all
//      per-axis branches from the inner loop;
//    * a short restart stack indexed by scale (no per-node push/pop list);
//    * LOD cut-off: stop descending once a voxel is smaller than a pixel.
//
//  This file is compiled by the OpenCL driver at runtime AND #included by the
//  host as plain C++ (see src/cl_shim.hpp) for the --cpu fallback, so it only
//  uses a small common subset of OpenCL C.
// ============================================================================

#ifdef __OPENCL_VERSION__
  #define F3(x, y, z) ((float3)((x), (y), (z)))
  #define KERNEL   __kernel
  #define GLOBAL   __global
  #define CONSTANT __constant
#endif

#define S_MAX     23          // float mantissa bits = max octree depth
#define MODE_SHADED  0
#define MODE_NORMALS 1
#define MODE_STEPS   2
#define MODE_DEPTH   3
#define PROJ_PERSPECTIVE 0
#define PROJ_FISHEYE     1

// Overlay box drawn on top of the scene by the ray tracer (build ghosts,
// selections...). Depth-correct and independent of the lens projection.
#define MAX_OVERLAYS 12
#define OVERLAY_FILL  0     // translucent fill + outline
#define OVERLAY_LINES 1     // outline only (style bit 0)
#define OVERLAY_ONTOP 2     // style bit 1: drawn even when behind the scene (gizmos)
typedef struct {
    float lo[3];            // box, octree space
    float hi[3];
    float color[4];         // rgb (display space) + fill opacity
    int   style;
} Overlay;

// A portal doorway: centre, right / up axes, and the normal of its entrance
// side (rays and the player go in against the normal). Half extents along
// right and up. The open door's entrance leads out of the other door's
// entrance side (a 180 degree turn about `up`).
typedef struct {
    float c[3];
    float r[3];
    float u[3];
    float n[3];
    float halfW;
    float halfH;
} PortalFrame;

// Must match struct Params in src/renderer.hpp (all 4-byte scalars, no padding).
typedef struct {
    float camPos[3];
    float camFwd[3];
    float camRight[3];
    float camUp[3];
    float sunDir[3];
    float tanHalfFov;
    float aspect;
    float lodCoef;     // voxel is "small enough" when size < lodCoef * distance
    int   width;
    int   height;
    int   mode;
    int   shadows;
    int   maxIters;
    int   projection;  // PROJ_PERSPECTIVE or PROJ_FISHEYE
    float halfFov;     // half the horizontal field of view, radians (fisheye)
    int   filter;      // bilinear colour filtering across neighbouring voxels
    int   texels;      // filter: N x N texels per voxel face (nearest), 0 = smooth
    float pixelAngle;  // angular size of one pixel (radians), for outline widths
    int   overlayCount;
    Overlay overlays[MAX_OVERLAYS];
    int   portalOpen;  // 1: rays entering portalIn continue out of portalOut
    int   portalMaxHops;
    PortalFrame portalIn;
    PortalFrame portalOut;
    int   ghostOn;     // 1: draw the ghost octree (a model held in hand), see-through
    float ghostOrigin[3];   // octree-space position of the ghost's root cube corner
    float ghostInvSize;     // 1 / edge length of the ghost's root cube
} Params;

// Node layout (2 x uint per node):
//   [0] index of first child (children of a node are contiguous, compacted)
//   [1] bits 0..7  : valid mask (child slot exists)
//       bits 8..15 : leaf mask  (child slot is a leaf voxel, do not descend)
// Child slot bits: bit0 = +x half, bit1 = +y half, bit2 = +z half.
// colors[i] holds RGBA8 for node i (interior nodes: average of children,
// used when the LOD cut-off stops traversal early).

typedef struct {
    float t;          // hit distance (ray dir is normalised), <0 = miss
    uint  node;       // index of the hit node (for colour lookup)
    int   axis;       // 0,1,2: which face was entered
    int   iters;      // traversal steps (for the heat-map mode)
    float size;       // edge length of the hit voxel (octree units)
} Hit;

// tOffset: distance already travelled before o (through portals), so the
// level-of-detail cut-off stays continuous across a portal.
Hit castRay(GLOBAL const uint* nodes, float3 o, float3 d,
            float tLimit, float lodCoef, int maxIters, float tOffset)
{
    Hit hit;
    hit.t = -1.0f; hit.node = 0; hit.axis = 0; hit.iters = 0; hit.size = 0.0f;

    const float eps = 1.1920929e-7f;   // 2^-23
    if (fabs(d.x) < eps) d.x = copysign(eps, d.x);
    if (fabs(d.y) < eps) d.y = copysign(eps, d.y);
    if (fabs(d.z) < eps) d.z = copysign(eps, d.z);

    // t(x) = x * coef - bias, evaluated in mirrored space.
    float tx_coef = 1.0f / -fabs(d.x);
    float ty_coef = 1.0f / -fabs(d.y);
    float tz_coef = 1.0f / -fabs(d.z);
    float tx_bias = tx_coef * o.x;
    float ty_bias = ty_coef * o.y;
    float tz_bias = tz_coef * o.z;

    int mirror = 0;   // axes where the ray is flipped (x' = 3 - x)
    if (d.x > 0.0f) { mirror |= 1; tx_bias = 3.0f * tx_coef - tx_bias; }
    if (d.y > 0.0f) { mirror |= 2; ty_bias = 3.0f * ty_coef - ty_bias; }
    if (d.z > 0.0f) { mirror |= 4; tz_bias = 3.0f * tz_coef - tz_bias; }

    // Active span of the root cube [1,2]^3.
    float t_min = fmax(fmax(2.0f * tx_coef - tx_bias, 2.0f * ty_coef - ty_bias), 2.0f * tz_coef - tz_bias);
    float t_max = fmin(fmin(tx_coef - tx_bias, ty_coef - ty_bias), tz_coef - tz_bias);
    t_min = fmax(t_min, 0.0f);
    t_max = fmin(t_max, tLimit);

    uint  stackNode[S_MAX + 1];
    float stackT[S_MAX + 1];

    uint  parent = 0;
    uint  descChild = 0, descMask = 0;
    int   haveDesc = 0;
    int   idx = 0;                         // child slot in mirrored space
    float3 pos = F3(1.0f, 1.0f, 1.0f);     // min corner of current child cube
    int   scale = S_MAX - 1;
    float scale_exp2 = 0.5f;               // child cube size = 2^(scale - S_MAX)

    // Pick the first child the ray enters.
    if (1.5f * tx_coef - tx_bias > t_min) { idx ^= 1; pos.x = 1.5f; }
    if (1.5f * ty_coef - ty_bias > t_min) { idx ^= 2; pos.y = 1.5f; }
    if (1.5f * tz_coef - tz_bias > t_min) { idx ^= 4; pos.z = 1.5f; }

    int found = 0;
    int iter = 0;
    while (scale < S_MAX) {
        if (++iter > maxIters) break;

        if (!haveDesc) {
            descChild = nodes[2u * parent];
            descMask  = nodes[2u * parent + 1u];
            haveDesc  = 1;
        }

        // t at which the ray leaves the current child cube.
        float tx_corner = pos.x * tx_coef - tx_bias;
        float ty_corner = pos.y * ty_coef - ty_bias;
        float tz_corner = pos.z * tz_coef - tz_bias;
        float tc_max = fmin(fmin(tx_corner, ty_corner), tz_corner);

        int  slot = idx ^ mirror;          // real (un-mirrored) child slot
        uint bit  = 1u << slot;

        if ((descMask & bit) != 0u && t_min <= t_max) {
            float tv_max = fmin(t_max, tc_max);
            float halfS  = scale_exp2 * 0.5f;
            float tx_center = halfS * tx_coef + tx_corner;
            float ty_center = halfS * ty_coef + ty_corner;
            float tz_center = halfS * tz_coef + tz_corner;

            if (t_min <= tv_max) {
                uint child = descChild + popcount(descMask & (bit - 1u));
                int  isLeaf = (descMask & (bit << 8)) != 0u;
                // (scale 1: the deepest the stack and float positions allow;
                // instances nested in instances can go that deep)
                int  lodHit = scale_exp2 < lodCoef * (t_min + tOffset) || scale <= 1;
                if (isLeaf || lodHit) {
                    hit.node = child;
                    found = 1;
                    break;
                }

                // PUSH: descend into this child.
                stackNode[scale] = parent;
                stackT[scale]    = t_max;
                parent = child;
                haveDesc = 0;

                idx = 0;
                scale--;
                scale_exp2 = halfS;
                if (tx_center > t_min) { idx ^= 1; pos.x += scale_exp2; }
                if (ty_center > t_min) { idx ^= 2; pos.y += scale_exp2; }
                if (tz_center > t_min) { idx ^= 4; pos.z += scale_exp2; }

                t_max = tv_max;
                continue;
            }
        }

        // ADVANCE to the next sibling along the ray.
        int step_mask = 0;
        if (tx_corner <= tc_max) { step_mask ^= 1; pos.x -= scale_exp2; }
        if (ty_corner <= tc_max) { step_mask ^= 2; pos.y -= scale_exp2; }
        if (tz_corner <= tc_max) { step_mask ^= 4; pos.z -= scale_exp2; }

        t_min = tc_max;
        idx ^= step_mask;

        // POP if we stepped out of the parent: the highest bit that differs
        // between the old and new position gives the scale to restart at.
        if ((idx & step_mask) != 0) {
            uint differing = 0u;
            if (step_mask & 1) differing |= as_uint(pos.x) ^ as_uint(pos.x + scale_exp2);
            if (step_mask & 2) differing |= as_uint(pos.y) ^ as_uint(pos.y + scale_exp2);
            if (step_mask & 4) differing |= as_uint(pos.z) ^ as_uint(pos.z + scale_exp2);
            scale = (as_int((float)differing) >> 23) - 127;
            if (scale >= S_MAX) break;     // left the root: miss
            scale_exp2 = as_float((scale - S_MAX + 127) << 23);

            parent = stackNode[scale];
            t_max  = stackT[scale];
            haveDesc = 0;

            int shx = as_int(pos.x) >> scale;
            int shy = as_int(pos.y) >> scale;
            int shz = as_int(pos.z) >> scale;
            pos.x = as_float(shx << scale);
            pos.y = as_float(shy << scale);
            pos.z = as_float(shz << scale);
            idx = (shx & 1) | ((shy & 1) << 1) | ((shz & 1) << 2);
        }
    }

    hit.iters = iter;
    if (found) {
        // The ray travels in -xyz in mirrored space, so it enters the cube
        // through its max-corner planes; the latest of those is the face hit.
        float ex = (pos.x + scale_exp2) * tx_coef - tx_bias;
        float ey = (pos.y + scale_exp2) * ty_coef - ty_bias;
        float ez = (pos.z + scale_exp2) * tz_coef - tz_bias;
        hit.axis = (ex >= ey && ex >= ez) ? 0 : (ey >= ez ? 1 : 2);
        hit.t = t_min;
        hit.size = scale_exp2;
    }
    return hit;
}

float3 unpackColor(uint c)
{
    return F3((float)(c & 255u), (float)((c >> 8) & 255u), (float)((c >> 16) & 255u)) * (1.0f / 255.0f);
}

// Point query: index of the node of edge `size` (or the larger leaf) that
// contains q, or -1 if that space is empty. One fetch per level.
int lookupNode(GLOBAL const uint* nodes, float3 q, float size)
{
    if (q.x < 1.0f || q.y < 1.0f || q.z < 1.0f || q.x >= 2.0f || q.y >= 2.0f || q.z >= 2.0f) return -1;
    uint node = 0;
    float3 c = F3(1.0f, 1.0f, 1.0f);   // min corner of the current node
    float h = 0.5f;                     // child edge length
    for (int i = 0; i < S_MAX; ++i) {
        int slot = 0;
        if (q.x >= c.x + h) { slot |= 1; c.x += h; }
        if (q.y >= c.y + h) { slot |= 2; c.y += h; }
        if (q.z >= c.z + h) { slot |= 4; c.z += h; }
        uint mask = nodes[2u * node + 1u];
        uint bit = 1u << slot;
        if ((mask & bit) == 0u) return -1;
        uint child = nodes[2u * node] + popcount(mask & (bit - 1u));
        if ((mask & (bit << 8)) != 0u || h <= size * 1.0001f) return (int)child;
        node = child;
        h *= 0.5f;
    }
    return -1;
}

// Bilinear filtering of voxel colours over the face that was hit: blend the
// hit voxel with its 3 nearest neighbours in the face plane, weighted by
// distance to their centres. Empty neighbours are skipped (and the weights
// renormalised) so colours never bleed in from the air.
float3 filteredAlbedo(GLOBAL const uint* nodes, GLOBAL const uint* colors,
                      float3 p, float3 n, Hit h, int texels)
{
    float s = h.size;
    float3 q = p - n * (0.5f * s);      // centre depth of the hit voxel layer
    float inv = 1.0f / s;

    // Tangent axes of the face: (a, b); the normal axis keeps q's coordinate.
    float qa = h.axis == 0 ? q.y : q.x;
    float qb = h.axis == 2 ? q.y : q.z;
    if (texels > 0) {
        // Nearest texel: snap the sample point to the centre of its texel in
        // an N x N grid on the voxel face, so the blend becomes pixel art.
        float tpv = (float)texels;
        qa = (floor(qa * inv * tpv) + 0.5f) * (s / tpv);
        qb = (floor(qb * inv * tpv) + 0.5f) * (s / tpv);
    }
    float fa = qa * inv - 0.5f, fb = qb * inv - 0.5f;
    float ia = floor(fa), ib = floor(fb);
    float wa = fa - ia, wb = fb - ib;

    // The hit voxel is one of the four: reuse it instead of a lookup.
    int selfA = wa >= 0.5f, selfB = wb >= 0.5f;
    float3 acc = unpackColor(colors[h.node]) * ((selfA ? wa : 1.0f - wa) * (selfB ? wb : 1.0f - wb));
    float wsum = (selfA ? wa : 1.0f - wa) * (selfB ? wb : 1.0f - wb);
    for (int k = 0; k < 4; ++k) {
        int ka = k & 1, kb = k >> 1;
        if (ka == selfA && kb == selfB) continue;
        float da = (float)ka, db = (float)kb;
        float w = (ka ? wa : 1.0f - wa) * (kb ? wb : 1.0f - wb);
        if (w <= 0.0f) continue;
        float ca = (ia + da + 0.5f) * s, cb = (ib + db + 0.5f) * s;
        float3 c = q;
        if (h.axis == 0) { c.y = ca; c.z = cb; }
        else if (h.axis == 1) { c.x = ca; c.z = cb; }
        else { c.x = ca; c.y = cb; }
        int idx = lookupNode(nodes, c, s);
        if (idx < 0) continue;
        acc = acc + unpackColor(colors[idx]) * w;
        wsum += w;
    }
    return acc * (1.0f / wsum);   // wsum >= 0.25 (the hit voxel's own weight)
}

uint packColor(float3 c)
{
    c.x = clamp(c.x, 0.0f, 1.0f);
    c.y = clamp(c.y, 0.0f, 1.0f);
    c.z = clamp(c.z, 0.0f, 1.0f);
    uint r = (uint)(c.x * 255.0f + 0.5f);
    uint g = (uint)(c.y * 255.0f + 0.5f);
    uint b = (uint)(c.z * 255.0f + 0.5f);
    return r | (g << 8) | (b << 16) | (255u << 24);
}

float3 skyColor(float3 d)
{
    float t = clamp(d.y * 0.5f + 0.5f, 0.0f, 1.0f);
    return F3(0.85f, 0.88f, 0.95f) * (1.0f - t) + F3(0.35f, 0.55f, 0.90f) * t;
}

// Draws the overlay boxes that are in front of the scene hit (hitT).
float3 applyOverlays(CONSTANT Params* P, float3 o, float3 d, float hitT, float3 col)
{
    const float eps = 1e-7f;
    float3 inv = F3(1.0f / (fabs(d.x) > eps ? d.x : copysign(eps, d.x)),
                    1.0f / (fabs(d.y) > eps ? d.y : copysign(eps, d.y)),
                    1.0f / (fabs(d.z) > eps ? d.z : copysign(eps, d.z)));
    for (int i = 0; i < P->overlayCount && i < MAX_OVERLAYS; ++i) {
        CONSTANT Overlay* ov = &P->overlays[i];
        float3 lo = F3(ov->lo[0], ov->lo[1], ov->lo[2]);
        float3 hi = F3(ov->hi[0], ov->hi[1], ov->hi[2]);
        float ax = (lo.x - o.x) * inv.x, bx = (hi.x - o.x) * inv.x;
        float ay = (lo.y - o.y) * inv.y, by = (hi.y - o.y) * inv.y;
        float az = (lo.z - o.z) * inv.z, bz = (hi.z - o.z) * inv.z;
        float nx = fmin(ax, bx), ny = fmin(ay, by), nz = fmin(az, bz);
        float tn = fmax(fmax(nx, ny), nz);
        float tf = fmin(fmin(fmax(ax, bx), fmax(ay, by)), fmax(az, bz));
        float te = fmax(tn, 0.0f);
        if (tf < te || (te > hitT && !(ov->style & OVERLAY_ONTOP))) continue;   // missed, or hidden behind the scene
        if (tn < 0.0f) continue;                     // camera inside the box: draw nothing

        // Distance from the entry point to the nearest edge of the entry face.
        float3 pe = o + d * te;
        float ex = fmin(pe.x - lo.x, hi.x - pe.x);
        float ey = fmin(pe.y - lo.y, hi.y - pe.y);
        float ez = fmin(pe.z - lo.z, hi.z - pe.z);
        float e;
        if (tn == nx)         e = fmin(ey, ez);
        else if (tn == ny)    e = fmin(ex, ez);
        else                  e = fmin(ex, ey);
        float lineW = 1.25f * P->pixelAngle * te + 1e-6f;

        float a = (ov->style & OVERLAY_LINES) ? 0.0f : ov->color[3];
        if (e < lineW) a = 0.9f;
        float3 c = F3(ov->color[0], ov->color[1], ov->color[2]);
        col = col * (1.0f - a) + c * a;
    }
    return col;
}

float3 pf3(CONSTANT float* v) { return F3(v[0], v[1], v[2]); }

// Distance to the open doorway along the ray, if the ray enters it from its
// entrance side within the rectangle; otherwise 1e30.
float rayPortal(CONSTANT PortalFrame* P, float3 o, float3 d)
{
    float3 n = pf3(P->n);
    float dn = dot(d, n);
    if (dn >= -1e-7f) return 1e30f;                      // parallel, or coming from behind
    float3 c = pf3(P->c);
    float t = dot(c - o, n) / dn;
    if (t <= 0.0f) return 1e30f;
    float3 q = o + d * t - c;
    if (fabs(dot(q, pf3(P->r))) > P->halfW || fabs(dot(q, pf3(P->u))) > P->halfH) return 1e30f;
    return t;
}

// Maps a point / direction from the entrance door's frame to the exit door's:
// (x, y, z) in the entrance frame -> (-x, y, -z) in the exit frame.
float3 portalMapDir(CONSTANT Params* P, float3 v)
{
    float x = dot(v, pf3(P->portalIn.r)), y = dot(v, pf3(P->portalIn.u)), z = dot(v, pf3(P->portalIn.n));
    return pf3(P->portalOut.r) * (-x) + pf3(P->portalOut.u) * y + pf3(P->portalOut.n) * (-z);
}

// The ghost: a second, small octree (a model being placed), marched in its
// own space. Points map as p_ghost = 1 + (p - ghostOrigin) * ghostInvSize, and
// the direction is scaled the same way, so t stays the scene's t. Drawn
// see-through where it is in front of the scene (first ray segment only).
float3 applyGhost(GLOBAL const uint* gNodes, GLOBAL const uint* gColors, CONSTANT Params* P,
                  float3 o, float3 d, float hitT, float3 col)
{
    const float inv = P->ghostInvSize;
    float3 go = (o - pf3(P->ghostOrigin)) * inv + F3(1.0f, 1.0f, 1.0f);
    float3 gd = d * inv;
    Hit g = castRay(gNodes, go, gd, hitT, 0.0f, P->maxIters, 0.0f);
    if (g.t < 0.0f) return col;
    float3 n = F3(0.0f, 0.0f, 0.0f);
    if (g.axis == 0) n.x = gd.x > 0.0f ? -1.0f : 1.0f;
    if (g.axis == 1) n.y = gd.y > 0.0f ? -1.0f : 1.0f;
    if (g.axis == 2) n.z = gd.z > 0.0f ? -1.0f : 1.0f;
    float3 L = F3(P->sunDir[0], P->sunDir[1], P->sunDir[2]);
    float3 c = unpackColor(gColors[g.node]) * (0.55f + 0.6f * fmax(dot(n, L), 0.0f));
    c = F3(sqrt(fmax(c.x, 0.0f)), sqrt(fmax(c.y, 0.0f)), sqrt(fmax(c.z, 0.0f)));
    return col * 0.3f + c * 0.7f;
}

KERNEL void render(GLOBAL const uint* nodes,
                   GLOBAL const uint* colors,
                   GLOBAL uint* out,
                   CONSTANT Params* P,
                   GLOBAL const uint* ghostNodes,
                   GLOBAL const uint* ghostColors)
{
    int px = (int)get_global_id(0);
    int py = (int)get_global_id(1);
    if (px >= P->width || py >= P->height) return;

    float3 o   = F3(P->camPos[0],   P->camPos[1],   P->camPos[2]);
    float3 fwd = F3(P->camFwd[0],   P->camFwd[1],   P->camFwd[2]);
    float3 rgt = F3(P->camRight[0], P->camRight[1], P->camRight[2]);
    float3 up  = F3(P->camUp[0],    P->camUp[1],    P->camUp[2]);

    // Screen position: x in [-1,1] across the width, y scaled by the same
    // factor (square pixels), +y up.
    float x = (px + 0.5f) / (float)P->width * 2.0f - 1.0f;
    float y = (1.0f - (py + 0.5f) / (float)P->height * 2.0f) / P->aspect;

    float3 d;
    if (P->projection == PROJ_FISHEYE) {
        // Equidistant fisheye: the angle from the view axis grows linearly
        // with the distance from the image centre (theta = r * halfFov), so
        // straight lines bend and a 180+ degree view fits on screen.
        float r = sqrt(x * x + y * y);
        float theta = r * P->halfFov;
        if (theta > 3.14159265f) {          // beyond straight behind: lens rim
            out[py * P->width + px] = packColor(F3(0.02f, 0.02f, 0.025f));
            return;
        }
        float s = r > 1e-6f ? sin(theta) / r : P->halfFov;
        d = fwd * cos(theta) + (rgt * x + up * y) * s;
    } else {
        float t = P->tanHalfFov * P->aspect;   // tanHalfFov is vertical
        d = normalize(fwd + (rgt * x + up * y) * t);
    }

    // March; if the ray reaches the open portal first, continue it out of the
    // other door (a few hops at most, for portals that see each other).
    float tAcc = 0.0f;                       // distance travelled before the current segment
    float3 o0 = o, d0 = d;                   // first segment, for the overlays (ghosts)
    float firstT = -1.0f;
    int hops = 0, iters = 0;
    Hit h;
    for (;;) {
        float tp = (P->portalOpen && hops < P->portalMaxHops) ? rayPortal(&P->portalIn, o, d) : 1e30f;
        h = castRay(nodes, o, d, tp < 1e30f ? tp : 1e30f, P->lodCoef, P->maxIters, tAcc);
        iters += h.iters;
        if (hops == 0) firstT = h.t >= 0.0f ? h.t : tp;
        if (h.t >= 0.0f || tp >= 1e30f) break;
        float3 q = o + d * tp - pf3(P->portalIn.c);
        o = pf3(P->portalOut.c) + portalMapDir(P, q);
        d = portalMapDir(P, d);
        o = o + d * 1e-6f;
        tAcc += tp;
        ++hops;
    }
    h.iters = iters;
    const float tTotal = tAcc + h.t;         // for fog and depth

    float3 col;
    if (P->mode == MODE_STEPS) {
        float s = clamp((float)h.iters / 192.0f, 0.0f, 1.0f);
        col = F3(s, s * s, 0.15f + 0.6f * (1.0f - s) * (h.t >= 0.0f ? 0.3f : 1.0f));
    } else if (h.t < 0.0f) {
        col = skyColor(d);
    } else {
        float3 n = F3(0.0f, 0.0f, 0.0f);
        if (h.axis == 0) n.x = d.x > 0.0f ? -1.0f : 1.0f;
        if (h.axis == 1) n.y = d.y > 0.0f ? -1.0f : 1.0f;
        if (h.axis == 2) n.z = d.z > 0.0f ? -1.0f : 1.0f;

        if (P->mode == MODE_NORMALS) {
            col = n * 0.5f + F3(0.5f, 0.5f, 0.5f);
        } else if (P->mode == MODE_DEPTH) {
            float z = clamp(1.0f - tTotal / 2.5f, 0.0f, 1.0f);
            col = F3(z, z, z);
        } else {
            float3 p = o + d * h.t;
            float3 albedo = P->filter ? filteredAlbedo(nodes, colors, p, n, h, P->texels)
                                      : unpackColor(colors[h.node]);
            if ((colors[h.node] >> 24) == 0xFEu)     // editor selection: tinted
                albedo = albedo * 0.45f + F3(1.0f, 0.78f, 0.25f) * 0.55f;
            float3 L = F3(P->sunDir[0], P->sunDir[1], P->sunDir[2]);

            float ndl = fmax(dot(n, L), 0.0f);
            float vis = 1.0f;
            if (P->shadows && ndl > 0.0f) {
                Hit s = castRay(nodes, p + n * 2e-4f, L, 10.0f, P->lodCoef, P->maxIters, tTotal);
                if (s.t >= 0.0f) vis = 0.0f;
            }
            float3 sky = skyColor(n);
            float3 light = sky * (0.30f + 0.15f * n.y) + F3(1.0f, 0.95f, 0.85f) * (1.1f * ndl * vis);
            col = albedo * light;

            // A touch of distance haze.
            float fog = clamp((tTotal - 0.6f) * 0.35f, 0.0f, 0.6f);
            col = col * (1.0f - fog) + skyColor(d) * fog;
        }
        // Gamma.
        col = F3(sqrt(fmax(col.x, 0.0f)), sqrt(fmax(col.y, 0.0f)), sqrt(fmax(col.z, 0.0f)));
    }
    if (P->ghostOn)
        col = applyGhost(ghostNodes, ghostColors, P, o0, d0, firstT >= 0.0f && firstT < 1e30f ? firstT : 1e30f, col);
    if (P->overlayCount > 0)
        col = applyOverlays(P, o0, d0, firstT >= 0.0f && firstT < 1e30f ? firstT : 1e30f, col);
    out[py * P->width + px] = packColor(col);
}
