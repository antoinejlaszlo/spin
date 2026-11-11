# Usage: cmake -DIN=file -DOUT=header.hpp -P embed.cmake
file(READ ${IN} HEX HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," BYTES "${HEX}")
string(REGEX REPLACE "(0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,)" "\\1\n" BYTES "${BYTES}")
file(WRITE ${OUT} "// Generated from kernels/octree.cl - do not edit.\n#pragma once\nstatic const unsigned char kOctreeKernelSource[] = {\n${BYTES}\n};\n")
