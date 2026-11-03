#!/bin/bash
# Packs the release files (WSL): Switch zip, Linux tar.gz, Windows zip, SHA256SUMS into /root/cvhd/release/<version>/.
#   scripts/package-release.sh <switch stage dir> [windows build dir, default /mnt/c/proj/cvhd/build-msvc]
# Build first, all at the same commit: switch/build.sh (+ the icon step), the Linux preset build in /root/cvhd/src,
# C:\proj\cvhd\win-build.cmd. The version comes from the staged NRO's cvhd_version.h ("0.<count>.<sha7>").
# The archives hold the recompiled program only; the script refuses game files (xex, dds, arc, ...).
set -eu
STAGE=${1:?switch stage dir, e.g. /root/cvhd/stage_0.292.6e2c091+}
WIN=${2:-/mnt/c/proj/cvhd/build-msvc}
LINUX=/root/cvhd/src/out/build/linux-amd64-release
SDKOUT=/root/cvhd/rexglue-sdk/out/linux-amd64
GIT_DIR=/mnt/d/git/dev/cvhd-recomp
V=$(grep -o '"0\.[0-9]*\.[0-9a-f]*' /root/cvhd/src/out/build/switch/cvhd_version.h | tr -d '"')
OUT=/root/cvhd/release/$V
rm -rf "$OUT"; mkdir -p "$OUT/pkg"; cd "$OUT"
docs() { cp "$GIT_DIR/README.md" "$1/README.md"; echo "$V" > "$1/VERSION"; }

# Switch: unzip to the SD root.
S=pkg/switch/switch/cvhd; mkdir -p $S/game
cp "$STAGE/cvhd.nro" "$STAGE/deko3d" $S/; cp -r "$STAGE/shaders" $S/
echo "Put default.xex and data/ from your own copy of the game here." > $S/game/PUT_GAME_FILES_HERE.txt
docs pkg/switch
(cd pkg/switch && zip -qr "../../cvhd-$V-switch.zip" .)

# Linux: cvhd + the SDK libraries + the six character modules; cvhd.sh sets the library path.
L=pkg/cvhd-$V-linux-x86_64; mkdir -p $L/game
cp "$LINUX/cvhd" "$LINUX"/libcvhd_dll*.so "$SDKOUT/librexruntime.so" "$SDKOUT/librexgpu-xenos.so" $L/
strip $L/cvhd $L/*.so
printf '#!/bin/sh\nd=$(cd "$(dirname "$0")" && pwd)\nexport LD_LIBRARY_PATH="$d${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"\ncd "$d"\nexec "$d/cvhd" "$@"\n' > $L/cvhd.sh
chmod +x $L/cvhd $L/cvhd.sh
echo "Put default.xex and data/ from your own copy of the game here." > $L/game/PUT_GAME_FILES_HERE.txt
docs $L
tar -C pkg -czf "cvhd-$V-linux-x86_64.tar.gz" "cvhd-$V-linux-x86_64"

# Windows: cvhd.exe + DLLs + the app-local MSVC runtime (no redistributable install needed).
W=pkg/cvhd-$V-windows-x86_64; mkdir -p $W/game
CRT=$(ls -d "/mnt/c/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Redist/MSVC/"*/x64/Microsoft.VC143.CRT | head -1)
cp "$WIN/cvhd.exe" "$WIN"/*.dll $W/
for n in msvcp140.dll msvcp140_atomic_wait.dll vcruntime140.dll vcruntime140_1.dll; do cp "$CRT/$n" $W/; done
echo "Put default.xex and data/ from your own copy of the game here." > $W/game/PUT_GAME_FILES_HERE.txt
docs $W
(cd pkg && zip -qr "../cvhd-$V-windows-x86_64.zip" "cvhd-$V-windows-x86_64")

bad=$(find pkg -type f \( -iname '*.xex' -o -iname '*.xexp' -o -iname '*.dds' -o -iname '*.arc' -o -iname '*.csr' -o -iname '*.opd' \
      -o -iname '*.col' -o -iname '*.txrc' -o -iname '*.xma' -o -iname '*.xwb' -o -iname 'dll[A-Z]*.dll' \)) || true
[ -z "$bad" ] || { echo "game data in the package: $bad" >&2; exit 1; }
rm -rf pkg
sha256sum cvhd-$V-* > SHA256SUMS
ls -la; cat SHA256SUMS
