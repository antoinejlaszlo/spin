#!/usr/bin/env bash
# Build (Release) and optionally run:  ./build.sh            -> build only
#                                       ./build.sh run [args] -> build, then render
set -euo pipefail
cd "$(dirname "$0")"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
echo "Built: build/octree_render"
if [[ "${1:-}" == "run" ]]; then
  shift
  ./build/octree_render "$@"
fi
