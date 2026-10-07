#!/usr/bin/env bash
# Configure and build everything into ./build
set -euo pipefail
cd "$(dirname "$0")/.."
cmake -S . -B build -G "${CMAKE_GENERATOR:-Ninja}" -DCMAKE_BUILD_TYPE="${BUILD_TYPE:-Release}" "$@"
cmake --build build -j"$(nproc)"
