// Just enough of OpenCL C, in C++, to compile kernels/octree.cl on the host.
// Used by the --cpu fallback (and to sanity-check the kernel without a GPU).
#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstddef>

namespace clshim {

typedef unsigned int uint;

struct float3 { float x, y, z; };
inline float3 operator+(float3 a, float3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline float3 operator-(float3 a, float3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline float3 operator*(float3 a, float s)  { return {a.x * s, a.y * s, a.z * s}; }
inline float3 operator*(float s, float3 a)  { return a * s; }
inline float3 operator*(float3 a, float3 b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
inline float  dot(float3 a, float3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float3 normalize(float3 a) { return a * (1.0f / std::sqrt(dot(a, a))); }

inline float fabs(float a) { return std::fabs(a); }
inline float fmin(float a, float b) { return std::fmin(a, b); }
inline float fmax(float a, float b) { return std::fmax(a, b); }
inline float sqrt(float a) { return std::sqrt(a); }
inline float sin(float a) { return std::sin(a); }
inline float cos(float a) { return std::cos(a); }
inline float floor(float a) { return std::floor(a); }
inline float copysign(float a, float b) { return std::copysign(a, b); }
inline float clamp(float x, float a, float b) { return x < a ? a : (x > b ? b : x); }

inline uint  as_uint(float f)  { uint u;  std::memcpy(&u, &f, 4); return u; }
inline int   as_int(float f)   { int i;   std::memcpy(&i, &f, 4); return i; }
inline float as_float(int i)   { float f; std::memcpy(&f, &i, 4); return f; }
inline uint  popcount(uint v) {
    v = v - ((v >> 1) & 0x55555555u);
    v = (v & 0x33333333u) + ((v >> 2) & 0x33333333u);
    return (((v + (v >> 4)) & 0x0F0F0F0Fu) * 0x01010101u) >> 24;
}

extern thread_local size_t g_global_id[2];
inline size_t get_global_id(int dim) { return g_global_id[dim]; }

#define F3(x, y, z) (::clshim::float3{(float)(x), (float)(y), (float)(z)})
#define KERNEL
#define GLOBAL
#define CONSTANT const

#include "../kernels/octree.cl"

#undef F3
#undef KERNEL
#undef GLOBAL
#undef CONSTANT
} // namespace clshim
