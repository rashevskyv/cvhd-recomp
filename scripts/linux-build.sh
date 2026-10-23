#!/bin/bash
# Incremental Linux build of the game (and the GPU plugin, with the deko3d-over-OpenGL test backend),
# then copies the plugin next to the exe (the SDK builds it in its own output directory).
#   CVHD_JOBS=4 scripts/linux-build.sh
set -eu
B=${CVHD_BUILD:-out/build/linux-amd64-release}
SDK=${REXSDK_DIR:-/home/user/rexglue/rexglue-sdk}
cmake --build "$B" -j"${CVHD_JOBS:-4}"
cp -f "$SDK/out/linux-amd64/librexgpu-xenos.so" "$B/librexgpu-xenos.so"
