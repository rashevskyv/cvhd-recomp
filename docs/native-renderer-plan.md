# Castlevania HD on Switch: the renderer plan (2026-10-22)

Decision rule from CLAUDE.md: the renderer is chosen by measurement on the console. This is the
plan for the step after the root-cause measurement, written while the `fsi` A/B waits for a run.

## 1. What the numbers say

Measured on Linux (same ring, same backend logic as the console) and on the console (0.84):

| per frame, title screen | count |
|---|---|
| draws | 119 |
| render-target switches (EDRAM ownership changes) | 16 |
| render-target transfer passes the host-render-target path issues for them | 111 |
| resolves (the 360's predicated tiling, tile by tile) | 12 |
| resolve clears | 4 |
| texture reloads (textures the game rewrites every frame) | 3, about 13 MB |
| vertex and index uploads | 2 MB |
| GPU time the command processor waits for at the next frame open | lavapipe ~500 ms, Tegra X1 via NVK ~478 ms |
| the same frame on the null backend (no GPU work) in Eden | 30 fps |

So the Xenia Vulkan backend's EDRAM emulation, not the recompiled code and not Horizon, costs
~478 ms per frame on the Tegra X1. sa2-nx and usf4-nx (the two ReXGlue Switch ports that reached
60 fps, GPL-3, ideas only) found the same: EDRAM emulation ate most of their GPU time until they
replaced the renderer.

## 2. The decision tree

1. **`render_target_path_vulkan = "fsi"` on the console (no build).** The fragment shader
   interlock path keeps EDRAM in one buffer: no ownership transfers, resolves stay. Outcome A: the
   fps lands near 30-60: keep the Xenia backend, tune it (section 3). Outcome B: still single
   digits or interlock is slow on Maxwell: go native (section 4). Either way, step 2 first.
2. **GPU timestamps per frame on the console** (`vkCmdWriteTimestamp` at frame open and after
   the swap, read back a frame later, one `[nx] gpu ms` line per second): the number that
   separates GPU time from everything else, for every later A/B.

## 3. If the Xenia backend stays: what to cut, by measurement

- Resolves: the game resolves its EDRAM tiles into the same textures every frame; a resolve that
  only feeds the next tile or the final composite can be an image copy without a format
  conversion pass, or skipped when the destination is overwritten before it is read (usf4-nx's
  "resolves nobody reads" and "clears into depth owners" rounds).
- Texture reloads: 13 MB per frame are textures the game rewrites (dynamic atlases or the
  resolved frame); check which with the texture cache's key log; a texture that is a resolve
  destination can be aliased to the resolve's image instead of being reloaded through guest
  memory.
- Transfers (if `fbo` stays): make the render target cache aware of the tiling pattern so a
  switch between tiles of the same surface moves nothing.
- Each cut is verified against lockstep captures (one vblank per swap) on Linux: the emulated
  output before and after must match frame by frame.

## 4. If native: a deko3d renderer at the Direct3D level

Why this shape: the game is a 2D side-scroller (sprite quads, alpha layers, a few effects). The
360 Direct3D is statically linked into the XEX; its entry points are plain guest functions. The
SDK's generated functions are weak aliases, so a host implementation of `sub_XXXXXXXX` replaces
the game's one at link time. sa2-nx's second road (an API-level renderer: hook about twenty
Direct3D functions, read the device's state at each draw, replay the frame on one render thread
through deko3d; no ring, no register file, no EDRAM model, no Vulkan driver) is the model.
deko3d also runs in Eden, so the emulator gate shows pictures.

Order of work, each step measured and checked:

1. **Census (Linux, one day).** Count which Direct3D functions the game calls and which write
   PM4 per frame, by overriding the suspected `sub_` entry points with counting wrappers that
   call the original. Identify them by signature: the draw (the one that precedes 119 draw
   packets per frame), SetTexture/SetRenderTarget/Resolve/Clear/Present (the `VdSwap` caller),
   shader binds, stream sources, render states. Cross-check with the ring statistics (119 draws,
   16 switches, 12 resolves). Expect 10-20 functions that matter.
2. **Device layout.** The offsets in the game's D3D device structure used by those functions
   (bound shaders, render targets, textures, blend and depth state). `sub_826275C0`'s fields
   (10896 = read-pointer writeback, 10908 = write position, 13408, 13568) are the first known
   ones.
3. **Shaders.** The game's Xenos shaders are few (12 pipelines at the title screen; a census of
   the whole game by playing through it on Linux with the pipeline log). Translate offline:
   the SDK's SPIR-V translator output -> SPIRV-Cross -> GLSL -> uam (deko3d's compiler), one
   DKSH per shader, shipped with the port. Nothing is translated at run time.
4. **The renderer.** One render thread. Draw records come from the hooks (a draw = shader pair,
   vertex layout from the vertex shader's fetches, textures, blend/depth state, viewport,
   scissor). Render targets are deko3d images per Direct3D surface; resolves are image copies;
   predicated tiling is ignored (the whole 1280x720 frame is one target). The present shows the
   front buffer through libnx's nwindow. Textures are untiled on first use and when the game
   rewrites them (the SDK's dirty tracking says when).
5. **Verification.** Lockstep captures on Linux: the emulated GPU is the reference; every
   difference is a single-frame dump until none is left (boot, menus, a stage, the map).
6. **Measure on the console.** Render thread ms per frame, game thread ms per frame, GPU time.
   sa2-nx's City Escape ends at 11.1 ms render / 15.5 ms game per frame; a 2D game should sit
   well under that.

What stays from the current work: the Horizon layer (memory arena, threads, clock, pads, logs,
the Eden gate), the recompiled code and its kernel, the measurements. The Xenia GPU plugin
stays as the Linux reference renderer.

## 5. Milestones

| milestone | done when |
|---|---|
| M0 fsi A/B + GPU timestamps | `[nx] gpu ms` per frame on the console, fsi fps known |
| M1 census | the list of the game's Direct3D entry points with per-frame call counts |
| M2 shaders offline | every shader of the title screen and one stage as DKSH |
| M3 first native frame in Eden | the title screen drawn by deko3d, compared with the Linux capture |
| M4 console | the title screen above 30 fps on the console, numbers in CLAUDE.md |
| M5 gameplay | one stage playable, lockstep-verified |

Licences: sa2-nx, usf4-nx and Hard Corps: Uprising are GPL; only their measurements and methods
are used here. This tree stays BSD-3 with the SDK.
