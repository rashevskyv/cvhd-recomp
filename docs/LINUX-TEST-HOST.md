# Linux test host (cloud box or any Ubuntu 24.04)

What this gives: the game runs on Linux x86-64 with the Vulkan backend (reference picture), the trace backend
(60 fps, no drawing, collects shaders) and the deko3d backend over OpenGL (`src/gpu/dk_gl`), headless.

## Build, from nothing

1. Packages (apt): `clang-20 cmake ninja-build libegl-dev libgl-dev libvulkan-dev mesa-vulkan-drivers xvfb xdotool
   imagemagick glslang-tools pulseaudio python3-pil` and the SDL build dependencies
   (`libx11-dev libxext-dev libxrandr-dev libxi-dev libxcursor-dev libxss-dev libxkbcommon-dev libxtst-dev
   libxfixes-dev libx11-xcb-dev libwayland-dev libdrm-dev libgbm-dev libasound2-dev libpulse-dev libdbus-1-dev
   libudev-dev`). The SDK needs clang 20 (clang 18 lacks `std::expected` with libstdc++ 13).
2. SDK: `git clone https://github.com/rexglue/rexglue-sdk`, check out `c94f5eb` ("Release v0.10.0", on `main`;
   the tag `v0.10.0` is two commits earlier), `git submodule update --init --depth 1` (skip moltenvk).
3. `git apply patches/sdk/*.patch` in the SDK, then `python3 switch/sdk-edits.py <sdk>` **once** on the clean SDK.
   A second run duplicates the Vulkan statistics insert in `command_processor.cpp` (known, harmless for the
   Switch flow, but reset the SDK with `git checkout -f c94f5eb` before re-running).
4. Game data: `gh release download gamedata -R rashevskyv/cvhd-recomp`, then
   `python3 -I tools/stfs_extract.py cvhd_LIVE_58410A7A.stfs game`. No `default.xexp` in `game/`.
5. CLI and codegen: build the SDK's `rexglue` target (`cmake --preset linux-amd64 -DCMAKE_C_COMPILER=clang-20
   -DCMAKE_CXX_COMPILER=clang++-20`), then `rexglue codegen cvhd_manifest.toml` in the repo (35 s; also
   codegen of the six character DLLs). Use `--ignore-stamp` after an SDK template edit and use the CLI the game
   build produces (`<sdk>/out/linux-amd64/rexglue`), not an older copy.
6. Game: `cmake --preset linux-amd64-release -DREXSDK_DIR=<sdk> -DCMAKE_C_COMPILER=clang-20
   -DCMAKE_CXX_COMPILER=clang++-20`, then `scripts/linux-build.sh` (about 40 minutes on 4 cores; it copies the
   GPU plugin next to the exe). `generated/rexglue.cmake` is tracked but rewritten by codegen:
   `git update-index --skip-worktree generated/rexglue.cmake` keeps the Linux version out of commits.
7. Sound: SDL3 here has no dummy audio driver. Run a PulseAudio daemon with a null sink
   (`XDG_RUNTIME_DIR=/tmp/xdg-runtime pulseaudio --start; pactl load-module module-null-sink`). The drive script
   uses `SDL_AUDIODRIVER=pulseaudio`. Without a working driver the game waits for its audio engine at the
   attract transition.

## Run

- Shaders: `python3 tools/xenos2glsl.py --all <dump dir> --out-dir <glsl dir>` (the trace backend writes
  `--dump_shaders=<dir>`), `cp src/gpu/shaders/present.vert <glsl>/present.vert.glsl` (and `.frag`). `glslangValidator`
  checks the output. Missing shaders are dumped to `<glsl>/missing/` by the backend; copy them to the dump dir.
- `scripts/linux-drive.py --out <dir> --steps "waitlog 'swap #540' 600; key Return; ..." -- --gpu_backend=trace
  --dump_shaders=<dir>` or `-- --gpu_backend=deko3d --deko3d_shader_dir=<glsl dir>/`.
  The trace backend logs `swap #N` only for N divisible by 60: wait on those.
- deko3d over OpenGL: `CVHD_DK_GL_DUMP=<dir>:<every N>` writes frames, `CVHD_DK_DUMP_DIR=<dir>` every uploaded
  texture, `CVHD_DK_GL_POISON=1` fills new images with noise (uninitialised-memory faults).
- One test at a time, kill by PID (never `pkill -f` with a pattern that the shell command itself contains).
  Keys: Return = Start, space = A, BackSpace = B, shift+Down = d-pad down (the SDK's keyboard driver, `--mnk_mode=true`).
