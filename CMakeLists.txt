cmake_minimum_required(VERSION 3.16)
project(octree_render LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(OCTREE_VIEWER "Build the real-time window (needs GLFW, downloaded automatically)" ON)

# Embed kernels/octree.cl into the executable as a byte array, regenerated
# whenever the kernel changes (no OpenCL SDK or runtime file lookup needed).
set(KERNEL_SRC ${CMAKE_CURRENT_SOURCE_DIR}/kernels/octree.cl)
set(KERNEL_HDR ${CMAKE_CURRENT_BINARY_DIR}/generated/kernel_source.hpp)
add_custom_command(
  OUTPUT  ${KERNEL_HDR}
  COMMAND ${CMAKE_COMMAND} -DIN=${KERNEL_SRC} -DOUT=${KERNEL_HDR} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed.cmake
  DEPENDS ${KERNEL_SRC} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed.cmake
  COMMENT "Embedding OpenCL kernel")

add_executable(octree_render
  src/main.cpp src/octree.cpp src/renderer.cpp src/physics.cpp src/orientation.cpp src/cl_loader.cpp
  src/voxel_store.cpp src/world.cpp src/build_tool.cpp src/portals.cpp src/portal_tool.cpp src/level.cpp src/start_tool.cpp src/move_tool.cpp src/world_file.cpp src/dream.cpp src/object_tool.cpp
  scenes/scenes.cpp scenes/flat/flat.cpp scenes/island/island.cpp
  src/cl_shim.hpp src/cl_loader.hpp src/octree.hpp src/renderer.hpp src/physics.hpp src/orientation.hpp src/scene.hpp
  src/voxel_store.hpp src/world.hpp src/build_tool.hpp src/portals.hpp src/portal_tool.hpp
  ${KERNEL_HDR})
target_include_directories(octree_render PRIVATE src ${CMAKE_CURRENT_BINARY_DIR}/generated)

find_package(Threads REQUIRED)
target_link_libraries(octree_render PRIVATE Threads::Threads ${CMAKE_DL_LIBS})

if(OCTREE_VIEWER)
  # GLFW: window + mouse/keyboard. Use an installed copy if there is one,
  # otherwise download and build it (zlib licence, ~1 MB, built once).
  find_package(glfw3 3.3 QUIET)
  if(NOT glfw3_FOUND)
    include(FetchContent)
    if(POLICY CMP0135)
      cmake_policy(SET CMP0135 NEW)
    endif()
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(glfw
      URL https://github.com/glfw/glfw/releases/download/3.4/glfw-3.4.zip)
    FetchContent_MakeAvailable(glfw)
  endif()
  set(OpenGL_GL_PREFERENCE GLVND)
  find_package(OpenGL REQUIRED)
  target_sources(octree_render PRIVATE src/viewer.cpp src/viewer.hpp src/build_ui.cpp src/build_ui.hpp)
  target_link_libraries(octree_render PRIVATE glfw OpenGL::GL)
  target_compile_definitions(octree_render PRIVATE OCTREE_VIEWER=1)
else()
  target_compile_definitions(octree_render PRIVATE OCTREE_VIEWER=0)
endif()

if(MSVC)
  target_compile_options(octree_render PRIVATE /O2 /W3)
  target_compile_definitions(octree_render PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
  set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT octree_render)
else()
  target_compile_options(octree_render PRIVATE -O3 -Wall -Wextra -Wno-unused-parameter)
endif()
