#!/bin/bash
# Builds the Switch NRO in WSL. Paths follow CLAUDE.md "Build layout on this PC":
#   sources /root/cvhd/src (rsync'd from the git checkout), SDK /root/cvhd/rexglue-sdk (stuntdouble's
#   patches/sdk + switch/sdk-edits.py applied), codegen output generated/default from the Linux step,
#   NVK /root/cbfd/nvk-sdk-real/lib/libvulkan.a, devkitPro /opt/devkitpro.
#   switch/build.sh [ninja targets...]
# Every run reconfigures with the version from git ("0.<commit count>.<sha7>", the NACP's display
# version, hbmenu shows it) and writes cvhd_version.h (the same string + the build time, the first
# log line), so a log and an NRO can always be matched to a commit.
set -eu
ROOT=${CVHD_ROOT:-/root/cvhd}
SRC=$ROOT/src
SDK=${REXSDK_DIR:-$ROOT/rexglue-sdk}
NVK_SDK=${NVK_SDK:-/root/cbfd/nvk-sdk-real}
GIT_DIR=${CVHD_GIT_DIR:-/mnt/d/git/dev/cvhd-recomp}
export DEVKITPRO=${DEVKITPRO:-/opt/devkitpro}
export PATH=$DEVKITPRO/tools/bin:$DEVKITPRO/devkitA64/bin:$PATH
BUILD=$SRC/out/build/switch

[ -f "$NVK_SDK/lib/libvulkan.a" ] || { echo "no NVK: $NVK_SDK/lib/libvulkan.a (cbfd-switch switch/build-nvk.sh)"; exit 1; }
[ -f "$SRC/generated/default/sources.cmake" ] || { echo "no codegen output: run the Linux build first"; exit 1; }

python3 "$SRC/switch/sdk-edits.py" "$SDK"

COUNT=$(git -C "$GIT_DIR" rev-list --count HEAD 2>/dev/null || echo 0)
SHA=$(git -C "$GIT_DIR" rev-parse --short=7 HEAD 2>/dev/null || echo unknown)
DIRTY=$(git -C "$GIT_DIR" status --porcelain 2>/dev/null | grep -q . && echo "+" || true)
VERSION="0.$COUNT.$SHA$DIRTY"
STAMP=$(date -u +%Y-%m-%dT%H:%MZ)
mkdir -p "$BUILD"
printf '#define CVHD_VERSION "%s"\n#define CVHD_BUILD_UTC "%s"\n' "$VERSION" "$STAMP" > "$BUILD/cvhd_version.h.new"
if ! cmp -s "$BUILD/cvhd_version.h.new" "$BUILD/cvhd_version.h" 2>/dev/null; then
    mv "$BUILD/cvhd_version.h.new" "$BUILD/cvhd_version.h"
else
    rm -f "$BUILD/cvhd_version.h.new"
fi
echo "version $VERSION, built $STAMP"

cmake -S "$SRC" -B "$BUILD" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$DEVKITPRO/cmake/Switch.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DREXSDK_DIR="$SDK" -DNVK_SDK="$NVK_SDK" -DCVHD_VERSION="$VERSION" \
    -DREXGLUE_ENABLE_TRACY=OFF -DREXGLUE_ENABLE_PERF_COUNTERS=OFF > "$BUILD/configure.log" 2>&1 \
    || { tail -20 "$BUILD/configure.log"; exit 1; }
cmake --build "$BUILD" --parallel "${CVHD_JOBS:-12}" ${1:+--target "$@"}
ls -la "$BUILD"/cvhd.nro "$BUILD"/cvhd.elf 2>/dev/null || true
sha1sum "$BUILD"/cvhd.nro 2>/dev/null || true
