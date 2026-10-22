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

## 6. M1 results: the census (2026-10-22)

Tool: `tools/census_gen.py` (`CVHD_CENSUS=ON`, Linux). Every recompiled function in 0x82600000-0x82660000 gets a
wrapper that counts calls, records callers (the link register), and measures the PM4 words the call wrote (the
device's write position +48 before and after, the packets decoded). Static call sites come from the generated code.
The game links **XDK 2.0.9328** (import library versions in the XEX header), the same Direct3D as Mushihimesama
Futari (futari-nx): its published device layout applies, and every offset below was confirmed in this game's code.

| Role | Function | Per frame (title) | How identified |
|---|---|---|---|
| the game's render-list executor (game code, inlined XDK state setters) | `sub_82608C98` | 2 calls | caller `sub_8260A2D8`; the only caller of every function below; writes dirty masks +16/+24/+32, decl +11992, the 0x2200 shadow |
| DrawVertices | `sub_82624FB8` | 6 | 1 site in the executor; `DRAW_INDX` + the dirty flush (8 register-writer sites), constants, fetch, shader load |
| DrawIndexedVertices | `sub_826253A8` | (see v5 run) | reads the bound index buffer +12612; same flush body |
| DrawVerticesUP | `sub_826257F8` | (see v5 run) | writes the 0x2280 shadow (+10612..+10656, inline fetch words 0x61412101) |
| DrawIndexedVerticesUP | `sub_82625C70` | (see v5 run) | as above plus the index data |
| dirty register flush writer (type-0 runs from a 64-bit mask) | `sub_8262CAF0` | 119 | 56 static sites: 8 in each draw, 6 in Resolve, 6 in Clear's core |
| ALU constants writer | `sub_8262CE88` | 10 | 2 sites per draw (VS then PS) |
| fetch constants writer | `sub_8262CD30` | 6 | 1 site per draw |
| shader load (IM_LOAD, LOAD_ALU_CONST literals) | `sub_8262DED8` | 6 | 1 site per draw; reads decl +11992, PS +12868, VS +12872 |
| surface setter (RB_SURFACE_INFO group, window scissor) | `sub_8262C758` | 16 | 1 site per draw, 2 in Resolve, Clear; compares RTs +12616..+12632 with +12904..+12920 |
| Clear | `sub_8262A140` -> `sub_8262A200` -> `sub_82629A90` (core) | 4 (8 rect draws) | IM_LOAD_IMM + DRAW_INDX_2 x2 per call |
| Resolve | `sub_8262AAF8` | 12 | arguments: Flags (bits 0-2 the surface, 4 = depth), rect, texture, point, slice, level, color, ClearZ in f1 |
| Swap | `sub_826287A8` | 1 | calls VdSwap; XE_SWAP |
| space function (segment switch, submits) | `sub_826274C0` | 3 | 115 static sites; called when +48 > +56 |
| ring wait / its poll | `sub_826275C0` / `sub_82620BD0` | 4.1 M polls | +10896 read-pointer write-back, +10908 |
| SetVertexShaderConstantF | `sub_82624530` | 171 | VMX byte-swap copy into +1920 + 16 x reg |
| blend state (0x2201 / 0x2208+RT, dirty bit 17) | `sub_8261EDB0` | 171 | writes +10552 + 4 x reg, mask +16 |
| SetPixelShader / SetVertexShader | `sub_826247D8` / `sub_82624A90` | | write +12868 / +12872 |
| SetStreamSource | `sub_8261E3E8` | | writes stride +12704, reads +12008 |
| vertex declaration bind | `sub_82620F10` | | writes +11992, calls the two shader setters and SetStreamSource |
| render target / depth core | `sub_8261E8E8` | | writes depth +12632, flags +10940, the 0x2000 and 0x2200 shadows |
| fence / lock helpers | `sub_826065B8`, `sub_82606698` (12 each), `sub_8263B630`/`sub_8263B638` (critical section) | 12-60 | the game waits here while another thread writes the ring |

Device layout confirmed (offset: proof): +0/+8/+16/+24/+32 dirty masks (the executor and the setters), +48 write
position / +52 segment end / +56 soft limit (`sub_8262CAF0`, `sub_8262C758`, the space function), +1920 VS constants
(`sub_82624530`), +10548 the 0x2200 register shadow (`sub_8261EDB0`), +10896 / +10908 ring read-pointer write-back
and write position (`sub_826275C0`), +10940..+10943 flags (`sub_8262C758`), +11992 declaration, +12008 bound stride,
+12612 index buffer, +12616..+12628 render targets, +12632 depth, +12704 stream stride, +12868 PS, +12872 VS. The
command buffer lives in the physical arena (0xBFA9xxxx); the device at 0x4002AC00. The ucode the GPU runs is the
patched copy (the declaration's formats written into the vfetch words): 10 shaders at the title screen, 48 containers
in `data/arc/shader.arc` for the whole game.

Open (not gating): the per-frame split between the four draws and why the fourth census run credited the draws
0 words (a trace run is in flight; the M3 hooks count themselves anyway).
