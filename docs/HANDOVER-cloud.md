# Handover to the cloud session (2026-10-23, local session "cvhd-m4-eden")

Goal of the next session: reach GAMEPLAY (the first stage playable) on whatever path works.
Everything below is in this repository at the commit named in the closing message of the local
session (`git log -1` on `origin/main`). CLAUDE.md "Findings" has the full history with evidence;
this file is the map.

## 1. State in one table

| screen | state in Eden (deko3d backend) | evidence |
|---|---|---|
| Konami logo | draws; its backdrop below the logo band is uninitialized image memory (see 6.4) | `E:\conker-data\eden-runs\cvhd-m3-title\logo-partial-0.105.png` (the person's screenshots, not on disk here) |
| title | complete, "Press the START button" draws | `E:\conker-data\eden-runs\cvhd-m4\title-press-start-0.107.png` |
| fade after START | full-screen black, correct | run `cvhd-0.111.511bfc3+-deko3d-gs1` shots 24-32 |
| main menu (the book) | renders, navigable | `E:\conker-data\eden-runs\cvhd-m4\menu-main-0.112.png` |
| Single Player page | renders, A/DOWN work | `E:\conker-data\eden-runs\cvhd-m4\menu-single-player-0.112.png` |
| "Start the Game" | the game loads, then calls into a character DLL: **`Execute(83022918): function not in function table`** | run `cvhd-0.112.93e0cbc+-deko3d-stage1`, `cvhd_rex_log.txt` 12:02:18 |

Frame rate in Eden: 17-38 fps, Eden's own JIT speed and the PC's load (other sessions build in WSL at
the same time); not a console number. Nothing has run on the console since build 0.84 (2.1 fps on the
Vulkan/NVK path, finding "THE ROOT CAUSE"). The deko3d backend has never run on hardware.

## 2. The path to gameplay: the character DLLs are XEX modules without recompiled code

`game/data/player_dll/` holds `dllAlucard.dll`, `dllAxeArmor.dll`, `dllCharlotte.dll`,
`dllJonathan.dll`, `dllShanoa.dll` (+1). After "Start the Game" the game loads one (the default
character is Alucard) and calls into it at `0x83022918`, outside the main image
(`0x82000000-0x82960000`): the recompilation covers `default.xex` only, so the SDK logs
`function not in function table` and the thread is gone. Every playable character is such a module.

What to do (in order; not started):
1. Confirm the module format (`xxd -l 16 dllAlucard.dll`: `XEX2`), its base address (0x83000000?) and
   exports (the SDK's `XexLoadImage` path: `src/system/user_module.cpp`, `XexModule`). The kernel
   trace (section 8.3) shows the load: look for `XexLoadImage` / `XexGetProcedureAddress` after the
   Single Player page.
2. Check whether ReXGlue codegen can take more than one image (manifest `cvhd_manifest.toml`; the
   CLI `rexglue codegen`; search the SDK for "modules", "images", "dll"). If yes: add the DLLs, run
   codegen, register their function tables when the module loads. If no: recompile each DLL as its
   own codegen run (its own manifest and output directory, base address from the XEX header) and
   extend the runtime's function table to several code ranges (the SDK registers one table for the
   main module: `Function table initialized for module: code=82210000-828AF4C8`).
3. Cheaper experiment first: a character whose code may be in the main XEX. The loading probes
   `data\opd\player\soma\...` (Soma) although there is no `dllSoma.dll`: "Change Characters" ->
   Soma -> "Start the Game" may reach the stage without any DLL. Navigation with `pad.txt`
   (section 8.2); the character list layout is unknown: screenshot each step.
4. The full licence: `--license_mask=4294967295` is a default of the Switch `main` since 022ce80 (the
   SDK's 0 made the game a trial: "Not available in the trial", "Unlock Full Game"). On Linux pass it on
   the command line.

## 3. What was found and fixed in this session (each item is in CLAUDE.md with its evidence)

| finding | fix / where |
|---|---|
| "Press START" invisible: the translator tracked written registers per register (`r0.z = ...` before `tfetch2D r0, r0.xy` hid the texcoord), and `tfetch1D` declared `sampler1D` while the image is 2D | `tools/xenos2glsl.py`: per-component tracking; 1D fetches sample `sampler2D` at (u, 0.5) |
| textures uploaded once, stale afterwards | the backend consumes the Switch dirty-page bytes before each draw (draw serials per page vs the image's fill serial); file reads mark pages (`rex_nx_mark_range` from the SDK's `xfile.cpp` edit) |
| Eden ignores deko3d's `fillRectangleEnable` (NV_fill_rectangle): rectangle lists drew one triangle (the fade's black triangle) | a pass-through geometry shader per vertex shader (`shader_<hash>.geom.dksh`, v3 = v1 + v2 - v0); cvar `deko3d_fill_rectangle=true` = the hardware mode for the console |
| directory opens failed (`game:\data\player_dll\` -> STATUS_NO_SUCH_FILE) | SDK edits: trailing separators stripped in `VirtualFileSystem::OpenFile`; on libnx a directory entry opens without a host handle |
| stuck behind the fade: the SDK's nop audio system refused to create a driver, so `XAudioRegisterRenderDriverClient` failed at boot and the game's audio engine never got a render callback; the transition waited on it | SDK edit: the nop system's driver "plays" each submitted frame (256 samples at 48 kHz = 5.33 ms) by releasing the client's semaphore; the audio worker stays |
| keyboard input in Eden unreliable (Qt focus) | `sdmc:/switch/cvhd/pad.txt`: scheduled pad presses read by `switch/host/input/nx_input_driver.cpp` (`38:START`, `100:A`, chords `A+UP`, `:<hold ms>`) |
| screenshots captured whatever lay on top of Eden (the person uses the PC) | `switch/eden-shot.ps1` captures with `PrintWindow`, no focus changes |
| text lost thin strokes | samplers now split min/mag filters (16 samplers) in 022ce80: **not yet verified** on a screenshot (the person's latest screenshot still shows dropped letters: see 6.1) |
| the SDK's `Shader::DumpUcode` throws on newlib (`std::filesystem::absolute`) | the backend writes the two dump files with stdio into `<shader dir>/missing/` |

Excluded as causes (with numbers): the host clock, sleeps, cache attributes, MMIO kicks, the fold,
memory, thread placement, WAIT_REG_MEM polls, Eden settings (identical boot dumps between a good and a
bad run), human interference (a run's early exit with the config written = Eden closed by hand).

## 4. The backend (what it is, where, how to enable)

- `src/gpu/deko3d_backend.cpp` (Switch only), `src/gpu/trace_backend.cpp` (the `trace` backend and the
  `rex_gpu_extra_backend()` entry), `src/gpu/ucode_hash.h`, `src/gpu/shaders/present.*`,
  `tools/xenos2glsl.py`; SDK hooks and host symbols: `docs/native-renderer-plan.md` section 8.
- Enable: `--gpu_backend=deko3d` (the Switch `main` adds it when `sdmc:/switch/cvhd/deko3d` exists),
  `--deko3d_shader_dir=` (default `sdmc:/switch/cvhd/shaders/`), markers `dk_dump` (image readback
  at swaps 120/600) and `dk_present_rt` next to that directory.
- Design: no EDRAM model; a host image per render target (EDRAM base + pitch), resolves are image
  copies into images keyed by guest address, textures at such addresses alias them, the swap presents
  the image at the front buffer's address. XDK fast clears recognised (`IsClearDraw`).
- Per-draw logs (once per shader pair, with a vertex dump; once per pair + texture set; distinct
  resolves and clears; every texture's first bind): grep `[dk]` in `cvhd_log.txt`.
- The Hard Corps: Uprising port (`D:\git\dev\hcu-switch`, session hcu-switch) syncs these files at a
  pinned cvhd commit (`switch/sync-cvhd.sh` there); tell that session about backend changes. Its two
  requests: the `sdmc:/switch/cvhd` strings as one constant; its WaitMultiple variant (HCU patch 0021).

## 5. Not covered by the backend yet

Depth resolves (ignored), culling (off: winding unverified), stencil, 1D/3D/cube textures (1D as 2D of
height 1 only), `k_1_5_5_5` / `k_5_6_5` channel order, indexed draws (code path untested), the image
pool is a bump allocator (nothing freed: 132 MB at the title, 185 MB at the menu; 448 MB pool since
022ce80), textures whose memory is reused with another size are "resized" by copying (the old content
is wrong until the dirty pages trigger a re-upload).

## 6. Open rendering defects at the menu (the person's screenshots, 0.112/0.113)

1. Thin glyph strokes dropped ("Purc'ase", "S'ngle"): the text is quads sampling `k_16` glyph strips
   (R16_Unorm, swizzle A00) through PS B3EC630B32280C8C with a 4096x1 RGBA8 lookup. The min/mag sampler
   split (022ce80) is the first candidate fix, unverified. If it stays: the strip's untiling (`k_16`
   16 bpp, `texture_conversion::Untile`), a half-texel offset, or the point-sampled scaled text.
2. Striped popup panel ("Not available in the trial" dialog: horizontal line pattern): a texture with
   the wrong pitch or block size (check the `[dk] texture at` line of the dialog's texture: format,
   tiled, pitch), or a `k_1_5_5_5` / `k_5_6_5` / DXT path.
3. The left column shows the same chapter card 5-6 times: wrong texture (the same image for different
   fetch constants: look at the `pairtex` lines for the card draws: same base? then the game writes
   different cards into one memory region between draws = a re-upload-within-a-frame case: the dirty
   check runs once per draw serial, see `checked_serial`), or wrong UVs (the 16-bit float texcoords).
4. Colour noise at the frame's edges and bottom: uninitialized image memory. Candidates: a texture
   whose image is larger than its uploaded data, the 1024x600 scene resolves (1024x608 images), or
   the background tile batch (VS D5A8AAF7EDC85E5D + PS 95304161F300020E, 50 quads) sampling outside a
   texture. The `pairtex` log names the texture per batch. Use `dk_dump` with a dump swap at the menu
   (add a cvar for the swap number; the dump prints 5 pixels and a 16x16 colour census per image) or
   write the images out as PPM.
5. "Loading." mirrored on the turning pages: the page-flip mesh samples the previous frame (the
   resolve images alias as textures); verify against a 360 video before calling it a bug (the back of a
   turning page is mirrored in the real game too). Sampler wrap modes: only repeat/clamp are mapped
   (`kRepeat` -> Repeat, everything else -> ClampToEdge); mirror modes are not: Xenos ClampMode 1
   (mirror) and 3 (mirror-once) would need `DkWrapMode_MirroredRepeat` / `MirrorClampToEdge`.

## 7. Open questions and tests that were asked for but not done

- Eden with the OpenGL host API instead of Vulkan, GPU accuracy Normal/High/Extreme, async GPU off:
  not tried. Eden's config: `E:\Switch\Eden\user\config\qt-config.ini` (keys `backend` / `use_vulkan`,
  `gpu_accuracy`, `use_asynchronous_gpu_emulation`); use a copy of the user folder; keep audio muted.
- Eden's debug options: its log `E:\Switch\Eden\user\log\eden_log.txt` (svc, nvdrv, vi warnings; it
  prints every setting at boot, which is how two runs' settings were compared); the GDB stub
  (Emulation > Configure > Debug, port 6543, aarch64 gdb) was not used; the sampler in our own log
  replaced it (`[nx] pc <thread>` every 2 s, addr2line on `cvhd.elf`).
- Frame time in Eden (50-300 ms when the PC is loaded, 26-45 ms otherwise): no deko3d-side profile yet.
  The SDK's `cp/s time` line (per second: batches, draws, swaps, idle waits) and the `[nx] pc` sampler
  are the tools; add timers around `IssueDraw` phases (uploads/untiling, descriptor writes, the
  per-draw `LogOnce` string building) and `IssueSwap` (fence wait).
- Why the fade starts by itself 25-40 s after the title in some runs and only after START in others
  (an attract timer that counts frames, or input): unknown; pad.txt makes START reliable anyway.

## 8. How to build and test

### 8.1 On this PC (Windows + WSL), the local session's procedure
- Sources: `D:\git\dev\cvhd-recomp` (git), mirrored with `rsync -a` (never `--delete` over
  `generated/`) to `/root/cvhd/src`; the SDK at `/root/cvhd/rexglue-sdk` (v0.10.0 `c94f5eb` +
  stuntdouble's `patches/sdk` + `switch/sdk-edits.py`, run by `switch/build.sh`; idempotent: two runs
  give the same `git diff --stat`, 9 known "anchor not found" warnings are expected).
- Game data: `/root/cvhd/game` (Linux) and `E:\Switch\Eden\user\sdmc\switch\cvhd\game` (Eden): the
  STFS extraction (`tools/stfs_extract.py`, `default.xex` + `data/`, no `default.xexp`).
- Codegen: `rexglue codegen cvhd_manifest.toml` in `/root/cvhd/src` (the host CLI
  `/root/cvhd/rexglue-sdk/out/linux-amd64/Release/rexglue`; 77 s; output `generated/default/`, not
  in git).
- Switch build: `bash switch/build.sh` in `/root/cvhd/src` (CVHD_JOBS=8; ~3 min incremental, ~12 min
  full) -> `out/build/switch/stage/` (NRO, `shaders/*.dksh` from `/root/cvhd/shaders_trace` =
  `$CVHD_SHADER_DUMPS`, the `deko3d` marker). The version `0.<commits>.<sha>+` is in the NRO's NACP
  and the log's first line.
- Eden run: `powershell -File switch/eden-deko3d.ps1 -Seconds 300 -Every 5 -Keys "38:START,100:A"
  -Tag name` -> `E:\conker-data\eden-runs\cvhd-<version>-deko3d-<name>\` (`shot-NN-0.png` every 5 s,
  `cvhd_log.txt` = our stdout log, `cvhd_rex_log.txt` = the SDK log, `eden_log.txt`). Extra cvars for a
  run: `E:\Switch\Eden\user\sdmc\switch\cvhd\args.txt`, one `--name=value` per line (e.g.
  `--log_noisy=true` + `--log_level=trace` for the kernel import trace; the SDK rotates the log at
  5 MB into `cvhd_rex_log.1.txt`). Missing shaders are dumped to `shaders/missing/` and copied to the
  dump directory by the script; rebuild to stage them.
- Timeline at Eden's speed: title at ~28 s, START at 38 s, fade at ~73 s, menu at ~77 s, A at 100 s ->
  Single Player page, DOWN x4 + A -> "Start the Game" -> the DLL call (section 2) at ~250 s.
- Console (never done for this backend): `switch/deploy.sh 192.168.50.69` pushes the NRO only; the
  `stage/shaders/` directory and the `deko3d` marker must also go to `sdmc:/switch/cvhd/`. Clocks and
  the real fps are unknown.

### 8.2 pad.txt
`<seconds>:<BUTTONS>[:<hold ms>]` per line, seconds from the pad driver's start, buttons START BACK A
B X Y LB RB LT RT UP DOWN LEFT RIGHT LS RS, chords with `+`, 200 ms hold by default. The game polls
the pad once per frame: at 10 fps a 200 ms hold can be missed (one DOWN of four was); use 400 ms.
The driver logs `[nx] pad: scheduled press buttons 0x.... at N s` for each press it delivered.

### 8.3 The kernel import trace
`args.txt` with `--log_noisy=true` and `--log_level=trace`: `[NtCreateFile]`, `[NtReadFile]`,
`[XexLoadImage]`-class calls appear in `cvhd_rex_log*.txt` with their thread ids; waits
(`KeWaitForSingleObject` etc.) are NOT traced. The GPU category floods the file at trace level
(5 MB per ~40 s): plan the run length.

## 9. What a Linux cloud box can and cannot do

Cannot: Eden (Windows), the Switch build's run (needs Eden or the console), the console. Can:
- The Linux x86-64 build: `cmake --preset linux-amd64-release -DREXSDK_DIR=<sdk>` then
  `cmake --build out/build/linux-amd64-release` (clang-20, Ninja; ~25 min; the SDK v0.10.0 with
  stuntdouble's `patches/sdk` and the non-Switch-guarded parts of `switch/sdk-edits.py` applied: the
  `OpenFile` trailing-separator fix and the `cp/s` statistics are platform-wide; `python3
  switch/sdk-edits.py <sdk>` applies all of them, the Switch-only ones are inside `#if
  REX_PLATFORM_SWITCH`). Needs the game data (359 MB, the person's copy; not in git) and codegen.
- `scripts/linux-run.sh <seconds> [cvhd args]`: Xvfb + lavapipe, `SDL_AUDIODRIVER=dummy`, screenshots
  every 10 s (`import -window root`), logs in `/root/cvhd/runs/<stamp>/`. Backends: the Vulkan path
  (the SDK's, 2-8 fps on lavapipe, correct picture), `--gpu_backend=null` (no drawing, ~30 fps),
  `--gpu_backend=trace --dump_shaders=<dir>` (logs every distinct draw/resolve/swap state, dumps the
  shaders for the deko3d staging). `CVHD_LOG_LEVEL=trace` sets the SDK log level.
- Input on Linux: the SDK's mouse-and-keyboard driver (`src/input/mnk`, cvars `keybind_*`): Return
  or X = Start, space = A, BackSpace = B, L = X, P = Y, WASD = left stick, Shift+arrows = d-pad.
  `CVHD_KEYS="40:Return,60:space" scripts/linux-run.sh ...` presses them with xdotool (installed in
  WSL), but in the one test the presses did not reach the game (the title stayed 100 s): the window
  search (`xdotool search --pid`) or the focus needs checking (`xdotool search --name`, `--class`,
  or `windowactivate`). A virtual gamepad (uinput + SDL) is the alternative. The Linux audio is SDL
  (dummy driver on a headless box: check that `XAudioRegisterRenderDriverClient` succeeds there too;
  the SDL audio system creates drivers, the nop one did not).
- Reference: the Linux Vulkan build showed the title correctly (finding "step 3"); the menu and the
  stage were never reached on Linux (no input). The `[trace]` state tables of the title are in
  `/root/cvhd/runs/20261022-193713/cvhd.log` (not in git).

## 10. Next steps, in order

1. Gameplay: the character DLLs (section 2): the Soma experiment, then multi-module recompilation.
2. The four menu defects (section 6) with a screenshot per fix, glyph columns first.
3. Frame-time profile of the backend in Eden (section 7), then the image pool allocator (free list +
   eviction of textures not bound for N frames; `GuestImage` resize/recreate leak).
4. Mirror wrap modes, culling (once a 3D-ish draw shows the winding), depth resolves if a stage needs
   them, indexed draws.
5. The console: stage directory + marker on the SD, `deko3d_fill_rectangle=true` A/B, the fps line, the
   `[nx] cpu` and `[nx] pc` lines at 1785/768 MHz.
