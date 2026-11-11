// Minimal runtime loader for the OpenCL ICD (OpenCL.dll / libOpenCL.so /
// OpenCL.framework). Declares only what this project needs, with the values
// from the Khronos headers, so building requires no OpenCL SDK at all —
// only a GPU driver at run time.
#pragma once
#include <cstddef>
#include <cstdint>

#if defined(_WIN32)
  #define CL_API_CALL __stdcall
#else
  #define CL_API_CALL
#endif

typedef int32_t  cl_int;
typedef uint32_t cl_uint;
typedef uint64_t cl_ulong;
typedef cl_ulong cl_bitfield;
typedef cl_bitfield cl_device_type;
typedef cl_bitfield cl_mem_flags;
typedef cl_bitfield cl_command_queue_properties;
typedef cl_uint  cl_platform_info;
typedef cl_uint  cl_device_info;
typedef cl_uint  cl_program_build_info;
typedef cl_uint  cl_bool;
typedef intptr_t cl_context_properties;

typedef struct _cl_platform_id*   cl_platform_id;
typedef struct _cl_device_id*     cl_device_id;
typedef struct _cl_context*       cl_context;
typedef struct _cl_command_queue* cl_command_queue;
typedef struct _cl_mem*           cl_mem;
typedef struct _cl_program*       cl_program;
typedef struct _cl_kernel*        cl_kernel;
typedef struct _cl_event*         cl_event;

#define CL_SUCCESS                0
#define CL_TRUE                   1
#define CL_FALSE                  0
#define CL_PLATFORM_NAME          0x0902
#define CL_DEVICE_TYPE_DEFAULT    (1 << 0)
#define CL_DEVICE_TYPE_CPU        (1 << 1)
#define CL_DEVICE_TYPE_GPU        (1 << 2)
#define CL_DEVICE_TYPE_ALL        0xFFFFFFFF
#define CL_DEVICE_TYPE            0x1000
#define CL_DEVICE_MAX_COMPUTE_UNITS 0x1002
#define CL_DEVICE_GLOBAL_MEM_SIZE 0x101F
#define CL_DEVICE_NAME            0x102B
#define CL_DEVICE_VERSION         0x102F
#define CL_CONTEXT_PLATFORM       0x1084
#define CL_MEM_READ_WRITE         (1 << 0)
#define CL_MEM_WRITE_ONLY         (1 << 1)
#define CL_MEM_READ_ONLY          (1 << 2)
#define CL_MEM_COPY_HOST_PTR      (1 << 5)
#define CL_PROGRAM_BUILD_LOG      0x1183

#define CL_FUNCS(X) \
  X(cl_int, clGetPlatformIDs, (cl_uint, cl_platform_id*, cl_uint*)) \
  X(cl_int, clGetPlatformInfo, (cl_platform_id, cl_platform_info, size_t, void*, size_t*)) \
  X(cl_int, clGetDeviceIDs, (cl_platform_id, cl_device_type, cl_uint, cl_device_id*, cl_uint*)) \
  X(cl_int, clGetDeviceInfo, (cl_device_id, cl_device_info, size_t, void*, size_t*)) \
  X(cl_context, clCreateContext, (const cl_context_properties*, cl_uint, const cl_device_id*, \
        void (CL_API_CALL*)(const char*, const void*, size_t, void*), void*, cl_int*)) \
  X(cl_command_queue, clCreateCommandQueue, (cl_context, cl_device_id, cl_command_queue_properties, cl_int*)) \
  X(cl_mem, clCreateBuffer, (cl_context, cl_mem_flags, size_t, void*, cl_int*)) \
  X(cl_program, clCreateProgramWithSource, (cl_context, cl_uint, const char**, const size_t*, cl_int*)) \
  X(cl_int, clBuildProgram, (cl_program, cl_uint, const cl_device_id*, const char*, \
        void (CL_API_CALL*)(cl_program, void*), void*)) \
  X(cl_int, clGetProgramBuildInfo, (cl_program, cl_device_id, cl_program_build_info, size_t, void*, size_t*)) \
  X(cl_kernel, clCreateKernel, (cl_program, const char*, cl_int*)) \
  X(cl_int, clSetKernelArg, (cl_kernel, cl_uint, size_t, const void*)) \
  X(cl_int, clEnqueueWriteBuffer, (cl_command_queue, cl_mem, cl_bool, size_t, size_t, const void*, \
        cl_uint, const cl_event*, cl_event*)) \
  X(cl_int, clEnqueueReadBuffer, (cl_command_queue, cl_mem, cl_bool, size_t, size_t, void*, \
        cl_uint, const cl_event*, cl_event*)) \
  X(cl_int, clEnqueueNDRangeKernel, (cl_command_queue, cl_kernel, cl_uint, const size_t*, const size_t*, \
        const size_t*, cl_uint, const cl_event*, cl_event*)) \
  X(cl_int, clFinish, (cl_command_queue)) \
  X(cl_int, clReleaseMemObject, (cl_mem)) \
  X(cl_int, clReleaseKernel, (cl_kernel)) \
  X(cl_int, clReleaseProgram, (cl_program)) \
  X(cl_int, clReleaseCommandQueue, (cl_command_queue)) \
  X(cl_int, clReleaseContext, (cl_context))

struct CLApi {
#define X(ret, name, args) ret (CL_API_CALL* name) args = nullptr;
    CL_FUNCS(X)
#undef X
    // Loads the OpenCL library. Returns false (and fills err) if unavailable.
    bool load(const char** err);
};

extern CLApi cl;
