#include "cl_loader.hpp"

#if defined(_WIN32)
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
  static void* openLib() { return (void*)LoadLibraryA("OpenCL.dll"); }
  static void* getSym(void* lib, const char* n) { return (void*)GetProcAddress((HMODULE)lib, n); }
#else
  #include <dlfcn.h>
  static void* openLib() {
      const char* names[] = {
  #if defined(__APPLE__)
          "/System/Library/Frameworks/OpenCL.framework/OpenCL",
  #endif
          "libOpenCL.so.1", "libOpenCL.so", nullptr };
      for (int i = 0; names[i]; ++i)
          if (void* h = dlopen(names[i], RTLD_NOW | RTLD_LOCAL)) return h;
      return nullptr;
  }
  static void* getSym(void* lib, const char* n) { return dlsym(lib, n); }
#endif

CLApi cl;

bool CLApi::load(const char** err) {
    void* lib = openLib();
    if (!lib) { *err = "OpenCL runtime library not found (install a GPU driver with OpenCL support)"; return false; }
#define X(ret, name, args) \
    name = reinterpret_cast<ret (CL_API_CALL*) args>(getSym(lib, #name)); \
    if (!name) { *err = "OpenCL library is missing " #name; return false; }
    CL_FUNCS(X)
#undef X
    return true;
}
