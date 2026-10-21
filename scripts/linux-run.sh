#!/bin/bash
# Runs the Linux build headless (Xvfb + lavapipe, sound off) and grabs screenshots.
#   scripts/linux-run.sh [seconds, default 90] [extra cvhd args...]
# Paths follow CLAUDE.md "Build layout on this PC": sources /root/cvhd/src, game /root/cvhd/game.
# Output: /root/cvhd/runs/<stamp>/{cvhd.log, shot-NN.png, stdout.txt}. One run at a time (kills by PID).
set -u
SECS=${1:-90}; shift || true
ROOT=${CVHD_ROOT:-/root/cvhd}
EXE=${CVHD_EXE:-$ROOT/src/out/build/linux-amd64-release/cvhd}
STAMP=$(date +%Y%m%d-%H%M%S)
OUT=$ROOT/runs/$STAMP
mkdir -p "$OUT"
export SDL_AUDIODRIVER=dummy
export VK_ICD_FILENAMES=${VK_ICD_FILENAMES:-/usr/share/vulkan/icd.d/lvp_icd.json}
export DISPLAY=:77
Xvfb :77 -screen 0 1280x720x24 >/dev/null 2>&1 &
XVFB=$!
sleep 1
cd "$(dirname "$EXE")"
"$EXE" --game_data_root="$ROOT/game" --gpu_plugin=xenos --fullscreen=false --audio_mute=true \
    --log_file="$OUT/cvhd.log" --log_level=info "$@" >"$OUT/stdout.txt" 2>&1 &
GAME=$!
echo "cvhd pid $GAME, output $OUT"
for ((t = 5, n = 0; t <= SECS; t += 10, n++)); do
    sleep 10
    kill -0 $GAME 2>/dev/null || { echo "cvhd exited at ${t}s"; break; }
    import -window root "$OUT/shot-$(printf %02d $n).png" 2>/dev/null
done
kill $GAME 2>/dev/null; sleep 1; kill -9 $GAME 2>/dev/null
kill $XVFB 2>/dev/null
echo "--- tail of $OUT/cvhd.log"; tail -30 "$OUT/cvhd.log" 2>/dev/null
echo "--- stdout"; tail -15 "$OUT/stdout.txt"
ls "$OUT"
