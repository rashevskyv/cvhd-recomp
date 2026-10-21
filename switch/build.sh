#!/bin/bash
# Builds the Switch NRO in WSL. Paths follow CLAUDE.md "Build layout on this PC":
#   sources /root/cvhd/src (rsync'd from the git checkout), SDK /root/cvhd/rexglue-sdk (stuntdouble's
#   patches/sdk + switch/sdk-edits.py applied), codegen output generated/default from the Linux step,
#   NVK /root/cbfd/nvk-sdk-real/lib/libvulkan.a, devkitPro /opt/devkitpro.
#   switch/build.sh [--configure] [ninja targets...]
set -eu
ROOT=${CVHD_ROOT:-/root/cvhd}
SRC=$ROOT/src
SDK=${REXSDK_DIR:-$ROOT/rexglue-sdk}
NVK_SDK=${NVK_SDK:-/root/cbfd/nvk-sdk-real}
export DEVKITPRO=${DEVKITPRO:-/opt/devkitpro}
export PATH=$DEVKITPRO/tools/bin:$DEVKITPRO/devkitA64/bin:$PATH
BUILD=$SRC/out/build/switch

[ -f "$NVK_SDK/lib/libvulkan.a" ] || { echo "no NVK: $NVK_SDK/lib/libvulkan.a (cbfd-switch switch/build-nvk.sh)"; exit 1; }
[ -f "$SRC/generated/default/sources.cmake" ] || { echo "no codegen output: run the Linux build first"; exit 1; }

python3 "$SRC/switch/sdk-edits.py" "$SDK"

if [ "${1:-}" = "--configure" ] || [ ! -f "$BUILD/build.ninja" ]; then
    [ "${1:-}" = "--configure" ] && shift
    cmake -S "$SRC" -B "$BUILD" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$DEVKITPRO/cmake/Switch.cmake" \
        -DCMAKE_BUILD_TYPE=Release \
        -DREXSDK_DIR="$SDK" -DNVK_SDK="$NVK_SDK" \
        -DREXGLUE_ENABLE_TRACY=OFF -DREXGLUE_ENABLE_PERF_COUNTERS=OFF
fi
cmake --build "$BUILD" --parallel "${CVHD_JOBS:-12}" ${1:+--target "$@"}
ls -la "$BUILD"/cvhd.nro "$BUILD"/cvhd.elf 2>/dev/null || true
