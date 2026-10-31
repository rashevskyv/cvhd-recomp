# Castlevania: Harmony of Despair (Xbox 360) → Nintendo Switch — notes for Claude

Goal: run the Xbox 360 XBLA game Castlevania: Harmony of Despair natively on Nintendo
Switch (libnx NRO) by static recompilation. PC is NOT a goal: the game has an official
Unity PC version on Steam, so a PC recompilation adds nothing. Linux x86-64 is used
only as the debug/profiling host.

This repo is `rashevskyv/cvhd-recomp` (private). Write every finding into "Findings"
below: symptom, cause, file, fix, how found, date.

## The one rule that overrides convenience: performance first

- The renderer API is chosen by measurement on the console, never by what is easier.
  Candidates on Switch: Vulkan through Mesa NVK (mesa-switch, static `libvulkan.a`,
  an SDK already built in `D:\git\dev\cbfd-switch`, see its CLAUDE.md "step 4" and
  `switch/build-nvk.sh`), and deko3d (Nintendo-native, devkitPro). Order: build on
  NVK first because the SDK exists; profile on hardware (GPU time, driver CPU time
  per frame, submit cost); if the driver's CPU cost dominates, port the backend to
  deko3d. Write the numbers and the decision here.
- The game is a 2D side-scroller (HD sprites, large backgrounds, up to 6 players,
  many alpha-blended layers): expect fill-rate and texture bandwidth to matter more
  than draw-call count. Measure before guessing.
- Clocks: the Xbox ports set CPU 1785 / GPU 768 / memory 1600 MHz through clkrst with
  a sys-clk / Horizon OC guard (`antoxa2584x/nfsu2-sw` `src/switch_nx.c`, our
  `D:\git\dev\conker-lr-xbox\src\switch_nx.c`). Reuse that.

## Toolkit

- **ReXGlue SDK** (https://github.com/rexglue/rexglue-sdk, built on hedge-dev
  XenonRecomp + XenosRecomp): PowerPC → portable C++, a host runtime (kernel, Xenos
  GPU via its own renderer, XMA audio, input), CI builds for win-amd64, linux-amd64
  and **linux-aarch64**. Check its LICENSE (NOASSERTION on GitHub: read the file) and
  every patch set's licence before copying code.
- Reference ports on ReXGlue to read for method (not to copy data): `giangra/
  stuntdouble-reignition` (manifest + SDK patches layout), `florinp93/hells-gate-recomp`,
  `sal063/AC6_recomp`, `FrankHUN88453/CoD3Recomp`. None targets Switch: this port is
  the first; the Switch host layer (memory, threads, VI surface, audio, pads, SD paths,
  SD log) comes from our N64 and Xbox ports (`D:\git\dev\cbfd-switch\host\switch\`,
  `D:\git\dev\conker-lr-xbox\src\switch_nx.c`, Horizon rules in
  `D:\git\dev\cbfd-switch\docs\switch-port\horizon-notes.md`).
- The game data (user's own copy, never commit): `D:\Downloads\x360\Castlevania Harmony
  of Despair\58410A7A\`: `000D0000\652844FE...` = the STFS `LIVE` container (378 MB,
  content type 0xD0000 arcade title, title id 0x58410A7A, not encrypted: `XEX2` is
  visible at 0x3D000), `TU в Cache\TU_1C422JQ_...` = title update (2.4 MB),
  `00000002\*` = 11 DLC containers (characters, BGM packs, chapters 7-11).
  Extract the XEX and the game files from the STFS with a Python STFS reader
  (write a small one or use an existing MIT tool), apply the title update's XEX if the
  recompilation should include the patch, and mount DLC as data.
- The XEX inside may be encrypted/compressed: XenonRecomp/ReXGlue decrypt retail XEX
  with the public retail key; check it loads before anything else.

## Plan (each step ends with a short report in "Findings")

1. Research: ReXGlue build on Linux x86-64 and its aarch64 path; its renderer backend(s)
   and what Vulkan features they need vs NVK on Tegra X1 (same table as the N64 port
   did); its runtime's platform assumptions (mmap/virtual memory for the 512 MB 360
   address space, threads, file I/O, dlopen); XMA audio decode cost; what the SDK's
   Switch gaps are. Decide the renderer path per the rule above.
2. Extract XEX + data, run the ReXGlue pipeline on Linux (WSL on this PC: /root/cvhd,
   never under /mnt/d), get the game to the title screen on Linux (SDL/Vulkan on
   lavapipe is fine for correctness; there is no host GPU Vulkan in WSL here).
3. Switch build: devkitA64 + libnx + NVK SDK; host layer; NRO; first run on the
   user's console (FTP 192.168.50.69:5000 via Kefir Hub; an MCP server on the console
   at http://192.168.50.69:12345/mcp offers `controller` and `cur_frame` (screen grab)
   for driving tests when the console is idle; coordinate: other sessions also use it).
4. Performance on hardware: profile, then the renderer decision (NVK vs deko3d), then
   the usual levers (clocks, thread pinning, caches, async present).
5. DLC and title update, multiplayer (local only), saves.

## Rules

- Never commit the game container, XEX, extracted data or recompiled output from the
  game. `.gitignore` covers `game/`, `gen/`, `build*/`, `*.xex`, `*.bin` data.
- Keep sound OFF on the PC (SDL_AUDIODRIVER=dummy; Eden cannot run this anyway).
- One Linux test at a time; kill by PID.
- Commit small, push to this repo; ask before pushing anything to other repos.
- The user writes Ukrainian and wants short, plain replies with numbers.

## Eden before the console (rule from 2026-10-21)

No NRO goes to the console before it ran in Eden (`E:\Switch\Eden\eden.exe`, portable `user\`; audio is
muted in its config). Eden cannot run NVK (Vulkan submissions never complete), so the Switch build has a
**no-GPU mode**: the file `sdmc:/switch/cvhd/nogpu` (Eden: `E:\Switch\Eden\user\sdmc\switch\cvhd\nogpu`)
selects the plugin's null backend (`--gpu_backend=null`, Findings "Null GPU backend"): the command
processor consumes the guest's commands and fires vblanks, nothing is drawn, no presenter; everything else is real: the memory arena and its folded views, the XEX load,
the kernel, threads on cores 0-2, fibers, the libnx pad, file I/O from `sdmc:`, the guest's main thread.
Procedure (`switch/build.sh` writes the NRO; stage it with the game data under Eden's `sdmc/switch/cvhd/`):
1. `powershell -File switch/eden-run.ps1 -Minutes 3`: stages the NRO from WSL, starts
   `eden.exe -g <nro>`, prints the swap count every 10 s, closes with `CloseMainWindow()` (force only
   after 15 s), backs the logs up, prints PASS or FAIL.
2. Pass = the log reaches the guest's frames (`[nx] gpu: swap #N` from the null backend, N growing) and
   the exit is clean; keep the logs in `E:\conker-data\eden-runs\cvhd-<version>\`.
3. Only then `switch/deploy.sh 192.168.50.69` (SHA-1 verified), and the console run checks the Vulkan path.
Since 2026-10-22 the picture gate supersedes the swap count: `powershell -File switch/eden-deko3d.ps1 -Seconds 90
-Every 15` runs the deko3d backend in Eden and keeps screenshots of every Eden window (Findings "M3 reached").
A console run of that backend needs `out/build/switch/stage/shaders/` and the `deko3d` marker next to the NRO
on the SD too; `switch/deploy.sh` pushes the NRO only (not done yet: no console runs requested).
Honest record: builds 0.29-0.35 went to the console without any Eden run; only the Linux build of the
same generated code had been verified. The Eden results follow in Findings.

## Findings

### 2026-10-21: step 1, research (ReXGlue v0.10.0 vs Switch)

**Toolkit facts** (read in `/root/cvhd/rexglue-sdk`, tag v0.10.0 = `c94f5eb`, 2026-08-21):
- Licence: BSD 3-Clause (Tom Clay; "portions derived from Xenia", BSD). Reference ports: stuntdouble-reignition MIT
  (its `patches/sdk/*.patch` are BSD, SDK terms); hells-gate-recomp replaces the guest D3D with its own DiligentCore
  renderer; AC6_recomp keeps the Xenia GPU; CoD3Recomp is XenonRecomp + its own D3D11 renderer, not ReXGlue.
- Flow (stuntdouble is the model): `rexglue init --project-name <n> --xex-path game/default.xex` writes
  `<n>_manifest.toml`, `generated/rexglue.cmake`, `src/main.cpp` (`REX_DEFINE_APP`), `CMakeLists.txt`;
  `rexglue codegen <manifest>` writes `generated/default/*.cpp`, `sources.cmake`, `<n>_pch.h`; the game CMake runs
  codegen as a build step and links `rex::runtime`. The GPU plugin must be named: `config.gpu_plugin = "xenos"` and
  `rexglue_setup_target(<n> GPU_PLUGINS xenos)`, otherwise the screen stays black (stuntdouble NOTES).
- Build: Clang >= 18 is enforced (`CMakeLists.txt` FATAL_ERROR), C++23, `-ffp-model=strict`, Ninja Multi-Config,
  presets `linux-amd64` / `linux-arm64` (`-march=armv8-a`). CI builds linux-arm64 on Ubuntu 24.04 ARM with clang-20.
- Layout: `rexruntime` is a **SHARED** library (core, kernel, system, audio, input, ui are OBJECT libs inside it);
  `rexgpu-xenos` is a **SHARED** plugin loaded at run time with dlopen (`src/system/gpu_plugin_loader.cpp`,
  `rex_gpu_abi_version` / `rex_gpu_create`). `rexcodegen` is static. Switch: both static, and `LoadGpuPlugin`
  replaced by a direct `rex_gpu_create` call.
- Generated code reads guest memory as `*(volatile u32*)(base + (u32)addr + REX_PHYS_HOST_OFFSET(addr))`
  (`resources/templates/codegen/pch_h.inja`); `REX_PHYS_HOST_OFFSET` is already a per-access compare on Windows and
  macOS-arm64 (the 0xE0000000 4 KB view offset), so a Switch address fold costs the same. MMIO (0x7F000000-0x7FFFFFFF,
  the GPU registers) is handled at compile time: codegen tracks `lis` of MMIO upper bits and emits `REX_MM_LOAD/STORE`
  which call `MMIOHandler::CheckLoad/CheckStore`. **No faults for MMIO.** Tail calls use `[[clang::musttail]]`
  (pch template, `DEFINE_REX_FUNC`).

**Renderer: Xenia's Vulkan backend (`src/graphics/vulkan`, `src/ui/vulkan`) vs NVK on the Tegra X1**
(NVK side from mesa-switch `0ff5207` `nvk_physical_device.c`, hardware-confirmed by the N64 port: Vulkan 1.4.354):

| Xenia Vulkan needs | NVK on Switch (GM20B) |
|---|---|
| Instance 1.1+, `VK_KHR_get_physical_device_properties2`, a platform surface: xcb / wayland / android / win32 / metal only | Vulkan 1.4; **`VK_NN_vi_surface` only** (`nvk_instance.c`, `wsi_common_switch.c`): add a `vkCreateViSurfaceNN(nwindowGetDefault())` path to `vulkan_presenter.cpp` and a Switch `Surface` type |
| Required features: independentBlend, fragmentStoresAndAtomics, vertexPipelineStoresAndAtomics, geometryShader (cvar `vulkan_require_geometry_shader`), fillModeNonSolid (cvar) | all `true` |
| Used when present: shaderClipDistance, shaderCullDistance, samplerMirrorClampToEdge, scalarBlockLayout, uniformBufferStandardLayout, shaderDemoteToHelperInvocation, dynamicRendering, nonSeamlessCubeMap, customBorderColors, robustness2 nullDescriptor, KHR_image_format_list, KHR_sampler_ycbcr_conversion, KHR_spirv_1_4 | all `true` |
| `EXT_fragment_shader_interlock` (ROV-style blending / depth parity) | `true` on MAXWELL_B..VOLTA_A, so yes on GM20B |
| `EXT_shader_stencil_export` (optional) | absent |
| sparseBinding + sparseResidencyBuffer for the 512 MB shared-memory buffer (cvar `vulkan_sparse_shared_memory`) | **false under `__SWITCH__`** (`nvk_sparse_binding_supported`: "no ctx_bind implementation"): falls back to one non-sparse 512 MB device buffer = 512 MB of RAM. Measure; if too much, shrink `kBufferSize` to the game's real physical use |
| Loader: `vulkan_instance.cpp` dlopens `libvulkan.so.1` / `.dylib` for `vkGetInstanceProcAddr` | none: take `vk_icdGetInstanceProcAddr` from the static `libvulkan.a` (as plume did in the N64 port) |
| SPIRV-Tools validation: dlopen (`spirv_tools_context.cpp`), optional | off |
| Shaders: Xenos ucode -> SPIR-V at run time by the SDK's own translator (`spirv_translator*.cpp`, glslang's `SpvBuilder` lib linked statically; no glslang front end, no DXC on the console) | fine; pipeline creation on NVK ~60 ms each (N64 port): keep a `VkPipelineCache` on the SD card |
| Presentation | FIFO / IMMEDIATE, 3 images, one queue (vktest) |

**Host dependencies** (devkitPro here: devkitA64 r30, GCC 16.1, newlib 4.6, libnx 4.12; portlibs listed below):

| Dependency | Used by | Switch |
|---|---|---|
| Clang >= 18 (`-ffp-model=strict`, `[[clang::musttail]]`) | everything | devkitA64 is GCC 16.1 (the N64 and Xbox ports use it). Either lift the Clang check when cross-compiling and build with GCC (`-ffp-contract=off` instead of `-ffp-model=strict`: GCC fuses mul+add on aarch64 by default; `[[gnu::musttail]]`), or drive clang-20 with `--target=aarch64-none-elf --sysroot=$DEVKITPRO/devkitA64/aarch64-none-elf --gcc-toolchain`. Try GCC first: smaller change, same toolchain as the other ports |
| mmap/shm (`core/memory_posix.cpp`, `system/xmemory.cpp`): one 4.5 GB file mapping, views listed below | guest memory | none: libnx virtmem + code memory (N64 port `nx_memory`), **aliasing to be probed** (see memory) |
| mprotect + SIGSEGV (`exception_handler_posix.cpp`, installed by `mmio_handler.cpp`) | **only** GPU SharedMemory write watches (`TriggerPhysicalMemoryCallbacks`) | libnx delivers user exceptions, but its `__libnx_exception_entry` pivots to `__libnx_exception_returnentry` -> handler -> `svcBreak`: it never resumes the faulting store (checked in `libnx.a` `exception.o`). `svcReturnFromException(0)` **does** resume with the TLS frame (switchbrew). Plan: our own `__libnx_exception_entry` (link order), unprotect the page, `svcReturnFromException(0)`. Reprotect needs `svcSetMemoryPermission` (or `svcSetProcessMemoryPermission`) on the mapped views: probe |
| dlopen (`dynlib_posix.cpp`): GPU plugin, Vulkan loader, SPIRV-Tools | runtime | none: static plugin, static NVK, no validation |
| pthreads: `pthread_create` with stack size, `SCHED_FIFO` priorities, `sched_setaffinity`, robust mutexes, TLS keys, `pthread_cancel` | `threading_posix.cpp`, `xthread.cpp` | libnx pthreads: no robust mutexes, no cancel, `pthread_detach` ENOSYS (N64 port `switch_compat.c`). Priorities/affinity through libnx `threadCreate`/`svcSetThreadCoreMask`: our own `threading_nx.cpp` is cleaner than patching posix. Guest threads get **16 MiB host stacks** (`xthread.cpp:433`): cap at 1-2 MB |
| Fibers (`fiber_posix.cpp`: makecontext/swapcontext) | `kernel/crt/threading.cpp` switches guest CRT threads onto host fibers (`Fiber::Create/SwitchTo`), `xthread.cpp` converts the main thread | newlib has no ucontext: write a small aarch64 `fiber_nx.cpp` (save/restore x19-x30, sp, d8-d15, ~40 lines of asm) |
| FFmpeg (vendored Xenia fork, XMA2 via `libavcodec`; its `config_linux_aarch64.h` needs glibc `arc4random`) | `audio/xma_*` | devkitPro `switch-ffmpeg 7.1-5` has the XMA decoder (`xma_decode_*` in `libavcodec.a`): link it instead of the vendored copy |
| SDL3 (window, input, audio) | `ui/window_sdl`, `input/sdl`, `audio/sdl` | **no SDL3 for libnx** (only `switch-sdl2 2.28.5`). SDK has `audio/nop` and `input/nop` drivers: boot with those, then write `audio/nx` (audout, 1024-frame buffers) and `input/nx` (hid pads), ~200 lines each, modelled on the sdl drivers. Window: no SDL; the presenter draws to the VI surface |
| fmt, spdlog, tomlplusplus, xxHash, simde (VMX128 -> NEON), o1heap, libmspack lzxd, tiny-aes, imgui, glslang SpvBuilder, VMA | all | portable C/C++, built from source for aarch64 |
| Tracy (sockets), perf counters overlay | profiling | Tracy off; `REXGLUE_ENABLE_PERF_COUNTERS` kept and written to the SD log |
| std::filesystem, fopen | `filesystem_posix.cpp`, saves, config | works on `sdmc:/`; no `copy_file`, no file times (N64/Xbox ports) |
| Sockets (`socket_posix.cpp`, xnet) | kernel xam net | libnx bsd sockets, or stub: local play only |

**Guest memory model** (`src/system/xmemory.cpp` `map_info`): one file mapping, views at `mapping_base + guest address`:
`0x00000000` (4 KB virtual, 1 GB), `0x40000000` (64 KB virtual, 1 GB - 16 MB), `0x7F000000` scratch, `0x80000000` and
`0x90000000` (the XEX, **both** at file offset 0x80000000), `0xA0000000`, `0xC0000000`, `0xE0000000` and the raw
physical view at `mapping_base + 0x100000000` (**all four** at file offset 0xA0000000, size 512 MB; the E0 view carries
a 4 KB host offset on Windows/mac). So the host needs 4 aliases of the 512 MB physical arena and 2 of the XEX.
Horizon has no mmap aliasing. Order of probes, in a 50-line NRO **before** the big Switch build:
1. `svcMapProcessMemory(dst, CUR_PROCESS_HANDLE, src, size)` on heap memory: true alias, R/W (loaders use it).
2. CodeMemory `MapOwner` + `MapSlave`: exactly two views, the slave read-only (not enough alone).
3. Fallback in software: `REX_PHYS_HOST_OFFSET` folds C0/E0/physical onto the A0 view, and `TranslatePhysical`
   (`xmemory.cpp`) returns the A0 pointer: the same compare per access that Windows pays. The 0x90000000 alias of
   the XEX gets the same fold.
Also probe: `svcSetMemoryPermission(..., Perm_R)` on an aliased page (write watches), fault resume through our own
`__libnx_exception_entry`, how many code-memory handles one process may hold (one per committed range).
Budget: 512 MB physical arena + XEX + virtual heaps committed on demand; title takeover gives ~3.2 GB (N64 port).

**Renderer decision: NVK first, measured; deko3d only if the driver's CPU time dominates.** NVK exists
(`/root/cbfd/nvk-sdk-real/lib/libvulkan.a`, 89 MB, mesa-switch `0ff5207`, 60 fps clears on hardware) and Xenia's
Vulkan backend needs nothing NVK lacks except sparse binding. What to measure on hardware, per frame, written to the
SD log every 2 s: (1) command-processor thread CPU ms (the Xenos emulation itself), (2) ms inside `vk*` calls on that
thread (driver CPU), (3) GPU ms (timestamp queries), (4) texture uploads: untiled bytes and count, (5) pipelines
created (cache hits included), (6) guest threads' CPU ms (the recompiled game). Decision rule: if (2) is the largest
share and (1)+(6) leave no headroom at 1785 MHz, port the backend to deko3d (a backend of the Xenos emulator is tens
of thousands of lines); the cheaper alternative for a 2D sprite game is hells-gate's route: replace the guest D3D
layer with a native renderer. If (1) or (6) dominates, the renderer API is not the problem.

**Environment pitfalls (this PC):**
- apt.llvm.org's clang-20 conflicts with Ubuntu 24.04.3's `libllvm20`; Ubuntu's own `clang-20` (20.1.2) installs
  fine: remove the llvm.org list first.
- Reading the 378 MB container through `/mnt/d` in WSL is very slow (9P seeks): extract on Windows into `game/`
  and stream into WSL with `tar -C game -cf - . | wsl -u root tar -xf - -C /root/cvhd/game`.
- lavapipe is present (`/usr/share/vulkan/icd.d/lvp_icd.json`), Xvfb installed.

### 2026-10-21: step 2, extraction and codegen

- **STFS container** (`tools/stfs_extract.py`, written from the Free60 layout): `LIVE`, title 58410A7A, content
  type 0xD0000, version 0x104, 91706 blocks, header base 0xB000, one hash table per 0xAA blocks (shift 0). It holds
  2161 files, 359 MB: `default.xex` (3,448,832 bytes, version 0.0.1.4, built 2010-07-09), `nxeart`, `ArcadeInfo.xml`,
  achievement PNGs, and `data/` (`arc`, `enemy`, `gimmick`, `menu`, `opd`, `player`, `player_dll`, `sound`, `stage`,
  `text`). Every file is contiguous. The title update `TU_1C422JQ_...` is also STFS: `default.xexp` (720,896 bytes)
  plus patched `data/` files and `ArcadeInfo.xml`. Extracted to `game/` and `game_tu/` (both git-ignored) and mirrored
  to `/root/cvhd/game`, `/root/cvhd/game_tu`.
- **Title update:** ReXGlue applies a `default.xexp` found next to `default.xex` at run time (`user_module.cpp`,
  `XexModule::ApplyPatch`), but codegen reads the base XEX only. A patch applied at run time would change code the
  recompilation does not have, so `game/` must **not** contain `default.xexp` for now. For the TU build: produce a
  patched `default.xex` offline (apply the delta once, write the image) and run codegen on it. Planned for step 5.
- **Codegen** (`rexglue codegen cvhd_manifest.toml`, the CLI built from the SDK with clang-20 in
  `/root/cvhd/rexglue-sdk/out/linux-amd64/Release`): the first run failed Validate with one `UnresolvedCall`
  (`b 0x826444A8 from 0x826444B4`, target in no function); declaring `0x826444A8` in the manifest fixed it, as in
  stuntdouble. Second run: 77 s, **34,336 functions**, 132 `.cpp` files + 132 headers, 136 MB, 0 `REX_FATAL`
  sites, 0 unresolved warnings. Warnings to watch at run time: 18 "Unexpected float16_4 pack instruction"
  (`vpkd3d128` FLOAT16_4, 0x82754A54-0x82755540: one shader-constant or vertex packing routine) and "Function
  0x828847A0 is 1357332 bytes" (a 1.3 MB function: probably discovery running through data; check if it is ever
  called).
- `rexglue init` resolves the `game` symlink and writes `../game`; edit the manifest back to `game/` (stuntdouble
  had the same). The runtime needs `gpu_plugin = "xenos"` (cvar, `cvhd.toml` next to the exe or `--gpu_plugin=xenos`)
  and `GPU_PLUGINS xenos` in `rexglue_setup_target`.
- Build layout on this PC: sources in `D:\git\dev\cvhd-recomp` (git), mirrored with `rsync` to `/root/cvhd/src`
  (`game` -> `/root/cvhd/game`), SDK at `/root/cvhd/rexglue-sdk`, build in `/root/cvhd/src/out/build/linux-amd64-release`
  (`cmake --preset linux-amd64-release -DREXSDK_DIR=/root/cvhd/rexglue-sdk`). Configure alone takes 10+ minutes
  (SDL3 and FFmpeg feature checks) while other builds run on this PC.

### 2026-10-21: Horizon memory probe on hardware (`switch/probe`, emuMMC, title takeover)

Log: `E:\conker-data\hub-logs\cvhd_probe_log.txt` (copy of `sdmc:/switch/cvhd/probe_log.txt`). Memory 3285 MB total,
3281 MB already in libnx's heap at start (title takeover). All SVCs hinted (0x73, 0x74, 0x77, 0x4B, 0x2C).

| Test | Result | Consequence |
|---|---|---|
| `svcMapProcessMemory(dst, own handle, heap src)` | **0xE401** (kernel InvalidCurrentMemory): heap memory cannot be mapped a second time | no R/W aliases of the arena |
| CodeMemory `MapOwner` (RW) + `MapSlave` (RX) | both rc 0; a write through the owner is seen through the slave | exactly two views: one RW, one read/execute. The owner view did **not** show the source heap's contents (`0xff`, not the `0x66` written before `svcCreateCodeMemory`): fill memory only after mapping |
| `svcSetMemoryPermission` / `svcSetProcessMemoryPermission(owner page, R)` | **0xD401** both | no page-permission toggling on the arena: no fault-based write watches |
| CodeMemory handles | **9** created, the 10th fails with 0xCE01 (resource limit) | the whole guest arena must fit in <= 8 code-memory blocks |
| `svcMapPhysicalMemory(reserved address, 1 MB)` | **0xFA01** | no on-demand commit at arbitrary addresses |
| Fault resume (test 4) | not reached (needs a reprotected page) | moot: nothing can be reprotected |
| Exit by + | crashed: Instruction Abort at a PC with no module, then Data Abort in hbl+0x5644 | returning to hbl with threads/handles alive. Use the N64 port's exit sequence (`cbfd-switch` `host/switch/switch_nx.cpp`, commit a1dabfe: stop own threads, libnx's own exit path once, then `svcExitProcess`) from the first real NRO |

**Switch memory design (decided from the table):**
- Reserve 4.5 GB of address space (libnx virtmem), as the N64 port did, and place at most 8 code-memory blocks
  at `base + guest address`, each backed by `malloc` from libnx's heap (RAM is there: 3.2 GB):
  `0x00000000` (4 KB virtual heap, 256 MB), `0x40000000` (64 KB virtual heap, 128 MB), `0x7F000000` (kernel
  scratch, 12 MB), `0x80000000` (XEX + kernel modules, 64 MB; the image is 0x82000000-0x82960000),
  `0xA0000000` (the physical arena, 512 MB). About 1 GB of RAM. Sizes are build constants; a heap request past a
  block's end fails loudly in the log. The ReXGlue heaps must allocate **bottom-up only** on the Switch (ignore
  the top-down flag), so the used part of each range stays inside its block.
- **No aliases: fold in software.** `REX_PHYS_HOST_OFFSET(addr)` on the Switch maps `0xC0000000-0xDFFFFFFF` and
  `0xE0000000-0xFFFFFFFF` onto the A0 block (`-0x20000000` / `-0x40000000`), and `0x90000000-0x9FFFFFFF` onto
  `0x80000000`. `physical_membase_` is the A0 block. `TranslatePhysical` and every host-side physical access go
  through the same fold. Cost: one compare per guest access (Windows already pays one for the E0 offset).
- **GPU write tracking without faults.** SharedMemory's watches (`xmemory.cpp` `TriggerPhysicalMemoryCallbacks`,
  installed through `mmio_handler.cpp`) need page reprotection, which Horizon refuses here. Replacement: a dirty
  byte per 4 KB physical page (128 KB array) set (a) by the Switch `REX_STORE_*` macros when
  `addr >= 0xA0000000` (the fold's compare is already there) and (b) by every host write into guest memory (file
  reads, kernel structures, XMA output). SharedMemory and the texture cache scan the array where they would
  have been told by a watch, then clear it. Fallback if the scan costs too much: coarse re-upload per frame.
- `switch/probe` stays in the repo as the hardware fact sheet; rerun it after a firmware or Atmosphère change.

**Launching things on the console:** give the main session the exact `sdmc:/switch/cvhd/...` path, the app name
and author hbmenu shows (`APP_TITLE`, `APP_AUTHOR` in the Makefile / nacp), and the buttons. The probe was launched
blind as "Unspecific Author".

### 2026-10-21: step 3, Linux boots to the title screen

- Build: `cmake --preset linux-amd64-release -DREXSDK_DIR=/root/cvhd/rexglue-sdk` then `cmake --build`, 1052
  steps, about 25 minutes on 12 jobs. Fixes on the way: `-mssse3 -msse4.1` for the SDK's byte-swap intrinsics,
  the imgui include path for `rex_app.cpp` (both as in stuntdouble), `LD_LIBRARY_PATH` to the SDK's output dir
  for `librexruntime.so` (`scripts/linux-run.sh`).
- Compile error in generated code: `use of undeclared label loc_8277F258` in `sub_8277ECF0`. The function is
  a second entry into `sub_8277EE40`'s body (it branches to 0x8277EE80, 0x8277F240, ...); discovery copied
  the body but cut it at 0x8277F134. Manifest: `[entrypoint.functions.0x8277ECF0] end = 0x8277F2C8`.
  Declaring the body as its own function did not help (the inner branch targets are not its start).
- First runs: "Call to invalid or unregistered function" at 0x822130A8, then 0x825F3398: callbacks reached
  through data pointers. stuntdouble's SDK patch set (`patches/sdk/`, BSD) fixes the class of problem:
  `dataPointerScan` found **21,080** more functions (126 interior addresses skipped), `codePointerScan` 173.
  With it, no unregistered call in 150 s.
- Result (`scripts/linux-run.sh 150`, Xvfb + lavapipe, `SDL_AUDIODRIVER=dummy`, `--audio_mute=true`): the
  Konami logo fades in at ~20 s, the title screen "Press the START button" is drawn correctly at ~45 s and
  stays (screenshots not committed: game content). Log: 0 fatal, 27 `NtCreateFile` failures for
  `D:\data\opd\...\*_idx.dds` / `*_uv.uvb` that exist nowhere (the game probes for optional files), 2 graphics
  pipelines per scene. SDL's audio init fails ("Audio target 'dummy' not available", the SDK's SDL3 has no dummy
  driver): sound is off, as wanted.
- XEX entry point 0x8263BA90 (`0x10100` header); image 0x82000000-0x82960000, code 0x82210000-0x828AF4C8.
- The step-4 GCC check (`aarch64-none-elf-g++ -std=gnu++23 -fsyntax-only` on generated and SDK files with a
  fake `__gnu_linux__`): the generated code and `memory.cpp`, `xmemory.cpp`, `filesystem.cpp`, `logging.cpp`,
  `socket_posix.cpp`, `system_posix.cpp` compile as they are. Gaps: `fiber.h` (ucontext), `threading_posix.cpp`
  (`cpu_set_t`, `pthread_setschedparam`, `pthread_sigqueue`, `sys/syscall.h`), `exception_handler_posix.cpp` and
  `seh_posix.cpp` (signals), `memory_posix.cpp` / `mapped_memory_posix.cpp` (`sys/mman.h`), `dynlib_posix.cpp`
  (`dlfcn.h`), `filesystem_posix.cpp` (`off64_t`, `fseeko64`), `clock_posix.cpp` (`CLOCK_MONOTONIC_RAW`),
  `vulkan/api.h` (wayland), `renderdoc_app.h` (platform), `xma_context.cpp` against devkitPro's FFmpeg 7.1
  (`AV_CODEC_ID_XMAFRAMES` is the Xenia fork's own decoder: the vendored FFmpeg must be cross-built instead).
  So GCC (devkitA64) is the Switch compiler; no Clang cross setup needed.

### 2026-10-21: step 4, the Switch build links (first NRO, not yet run on hardware)

- **Toolchain:** devkitA64 GCC 16.1 through `$DEVKITPRO/cmake/Switch.cmake`, Ninja, Release. The SDK is
  edited for the Switch by `switch/sdk-edits.py` (idempotent, run by `switch/build.sh` on top of
  stuntdouble's `patches/sdk`); the script is the record, there is no separate .patch file because two of
  the edits are inside the FFmpeg submodule. `switch/build.sh` configures and builds
  `/root/cvhd/src/out/build/switch/cvhd.nro`; `switch/deploy.sh <ip>` pushes it.
- **Numbers:** 141 build steps for the game and the SDK (the 132 generated files take ~3 minutes on 12
  jobs; the whole tree ~12 minutes); `cvhd.elf` 680 MB with debug info, `.text` 110 MB, `cvhd.nro` 111 MB.
- **What the SDK edits do:** `REX_PLATFORM_SWITCH` (+ `REX_PLATFORM_LINUX`, not GNU_LINUX) in
  `platform.h`; GCC allowed and `-ffp-contract=off` instead of `-ffp-model=strict`; `rexruntime` and
  `rexgpu-xenos` static, the plugin created by a direct `rex_gpu_create` call; no SDL3 (an INTERFACE
  stand-in); `windowed_app_main_nx.cpp` as the entry; `VK_NN_vi_surface` through `api.h`, `instance.h`
  (`ext_NN_vi_surface`, the functions `.inc`), `vulkan_instance.cpp` (`vk_icdGetInstanceProcAddr` from the
  static NVK, no loader), `vulkan_presenter.cpp` (`NnViSurface` case) and `surface.h` (`kTypeIndex_NnVi`);
  `threading_switch.cpp` generated from `threading_posix.cpp` (no signals, no robust mutexes, pthread_t
  ids); `input_backend` "nop" and `NopAudioSystem`; FFmpeg `config_switch_aarch64.h` (no arc4random,
  mmap, strerror_r...); glslang's `OSDependent` built by forcing `UNIX` for it; `fiber.h` with a 24-word
  context; the guest-memory fold in `xmemory.h` / `xmemory.cpp` and in the codegen pch template.
- **Host layer** (`switch/host/`): `memory_nx.cpp` (4.5 GB virtmem reservation, 5 code-memory blocks:
  256 + 128 + 12 + 64 + 512 MB, the other views folded), `mapped_memory_nx.cpp` (files read into RAM),
  `fiber_nx.cpp` (callee-saved context switch in asm), `exception_handler_nx.cpp` / `seh_nx.cpp` (no
  faults), `ui/` (one window on the default nwindow, a UI loop on `appletMainLoop` + a UEvent, paint
  requests from the presenter), `switch_nx.cpp` (SD log `sdmc:/switch/cvhd/cvhd_log.txt` flushed and
  fsync'd twice a second, clkrst 1785/768/1600 MHz with the sys-clk guard, 2 MB thread stacks through
  `--wrap=__syscall_thread_create`, 32 MB nvdrv transfer memory, `svcExitProcess` exit, crash register
  dump), `switch_compat.c` (timegm, posix_memalign, affinity/priority shims, pread/pwrite, creat, stubs).
- **GCC pitfalls found:** (1) `__attribute__((alias))` *before* `extern "C"` is silently ignored by GCC
  ("attributes may be inserted here"), so every `sub_*` alias was missing: 34,151 undefined references.
  The pch template now puts the attributes after the declarator (clang accepts both). (2) `-include` on
  `add_compile_options` reaches assembly files: guard it with `$<COMPILE_LANGUAGE:C,CXX>`. (3) strict
  `-std=c++23` hides `fileno`, `fdopen`, `tm_gmtoff` in newlib: `_GNU_SOURCE`, and `SPDLOG_NO_TZ_OFFSET`.
  (4) `strerror_r` is the GNU `char*` form under `_GNU_SOURCE` (FFmpeg `AVERROR(-ptr)`): `HAVE_STRERROR_R 0`.
  (5) `vulkan.hpp`'s DynamicLoader has no Horizon branch: `VULKAN_HPP_ENABLE_DYNAMIC_LOADER_TOOL=0`.
  (6) codegen rewrites `generated/rexglue.cmake`, which needs the `rex::rexglue` CLI target: the cross
  build imports the host-built CLI as that target (`REXGLUE_HOST_CLI`).
- **GPU write tracking (done, not yet run):** the codegen pch's `REX_STORE_*`, `REX_MM_STORE_*`, vector
  (`stvx`), reservation (`stwcx`) and `dcbz` stores set one byte per 4 KB page of the physical arena
  (`rex_nx_dirty`, 128 KB) on the Switch; `SharedMemory::RequestRanges` calls
  `rex::memory::nx::FlushDirtyPages`, which feeds each dirty page into the SDK's own
  `TriggerPhysicalMemoryCallbacks` (the same bookkeeping the page faults fed). File reads into physical
  memory already trigger it in `xfile.cpp`. Cost: a compare and a byte store per guest store; the NRO
  grew from 111 MB to 142 MB. The codegen builders (`memory.cpp`, `helpers.h`, `system.cpp`) emit
  `REX_RAW_STORE_ADDR` for those stores; it is `REX_RAW_ADDR` on every other platform.
- **Corrections before the first run (advisor review, 2026-10-21):**
  - `0x7F000000-0x7FFFFFFF` is not kernel scratch: `map_info` targets it at file offset `0x100000000`, the
    GPU writeback window = physical 0-16 MB (ring-buffer read pointer, fences). A block of its own would
    hide the GPU's writes and the game would wait forever. It is folded onto the A0 block (`+0x21000000`)
    in `TranslateVirtual` and `rex_nx_host_offset`; the blocks are now 4: 256 + 128 + 64 + 512 MB.
  - `XFile::Read/Write` bounce through a heap buffer on the Switch: libnx's fs IPC must never see a
    code-memory mapping (it jams the fs service; Horizon notes of the Xbox port).
  - Commits outside the mapped blocks return null with an `[nx_memory]` line instead of a later data abort;
    `MapFileView` logs when a view asks for more than its block.
  - The scalar store macros mark the page *after* the store, so a flush between the two cannot lose the
    write. The flush scans a 512-byte coarse map (1 MB per byte) before the 128 KB fine map.
  - The "one compare per access" is a short chain for E0/C0/90/7F addresses; most accesses take the
    first branch.
  - Known gaps from `threading_switch.cpp`: no signals, so `Thread::Suspend` of a running thread does
    nothing and user callbacks (APCs) are delivered only at alertable waits, not immediately.
  - Threads: every thread still starts on core 0 at libnx's default priority (the compat shims act only on
    the calling thread). For the performance step, copy the Xbox port's registry (`xbox_nx_spread_thread`,
    `xbox_nx_thread_at` in `conker-lr-xbox/src/switch_nx.c`) into `__wrap___syscall_thread_create`:
    cores 0-2, priorities 0x29-0x2E.
  - First log lines to expect, in order: `[nx_memory] reserved`, four `mapped guest` lines, `[clock]`,
    then in `cvhd_rex_log.txt`: the `NVIDIA Tegra X1` device, the swapchain, `Guest memory arena mapped`,
    `Function table initialized`, `Creating graphics pipeline`. The non-sparse 512 MB shared-memory
    buffer is the first allocation that may fail on NVK; the log names it.
- **Not done yet, in order:** input (nop: no controls), audio (nop), the pipeline cache on the SD card, the
  title update, the `[rt64]`-style frame-time split for the renderer decision. The first hardware run
  (the 11:55 NRO) is pending: the console went offline during the FTP push, so neither the NRO nor the
  game data (`sdmc:/switch/cvhd/game/`) are confirmed on the SD.
- **On the console:** `sdmc:/switch/cvhd/cvhd.nro` ("Castlevania HD" by cvhd-recomp in hbmenu, title
  takeover: hold R on a game), game data in `sdmc:/switch/cvhd/game/` (`default.xex`, `data/`; no
  `default.xexp`). Logs: `sdmc:/switch/cvhd/cvhd_log.txt` (stdout: `[nx]`, `[nx_memory]`, `[clock]`
  lines, crash dump) and `cvhd_rex_log.txt` (the SDK's log). Exit: HOME, then close the software.

### 2026-10-21: first hardware run (12:22 NRO): crash at pc 0 after the window

- The user's two runs (13:51 and 14:36 UTC) are identical: boot is fine (3285 MB, clocks 1785/768/1600,
  NVK's feature list printed), then after `[nx] window 1280x720 on the default nwindow` the handler
  prints `crash: exception 0x100, pc 0x0, far 0x0`: a call through a null function pointer.
  Atmosphère's trace, symbolized with `aarch64-none-elf-addr2line -e out/build/switch/cvhd.elf`:
  `Presenter::UpdateSurfacePaintConnectionFromUIThread` <- `SetWindowSurfaceFromUIThread` <-
  `ReXApp::SetupPresentation` <- `ReXApp::OnInitialize`.
- Cause: `vkCreateViSurfaceNN` was declared in the instance's function table (`instance.h`) but never
  loaded: `vulkan_instance.cpp` loads each surface extension's functions in its own `#ifdef` block, and
  only xcb/wayland/android/win32/metal had one. Fixed in `sdk-edits.py`.
- `windowed_app_main_nx.cpp` now prints a `[nx]` step line (flushed) before and after `OnInitialize` and
  the UI loop, and `WindowNX::CreateSurfaceImpl` logs the allowed surface types.
- Note: the `[nx] ... build <date>` line shows when `switch_nx.cpp` was last compiled, not when the NRO
  was linked; the NRO's size and the FTP timestamp tell the build apart.

### 2026-10-21: second hardware run (0.29.b029141): the renderer is up, the guest heaps fail

- `[nx] presenter ... slot4 0x73a1f43690 ... type 5 1280x720` and `[nx] vkCreateViSurfaceNN 0x73a1acd44c`:
  the pc=0 crash is gone (its cause was the unloaded `vkCreateViSurfaceNN`; the 14:43 run that "still
  crashed" was the old NRO: same size, no version string. Hence the version in the NACP and the log).
- Then `[nx_memory] reserved 4608 MB`, the blocks mapped, and the kernel's allocations fail: (1) `commit
  of guest 0xC0000000 + 0x1000000`: the 16 MB-page physical heap commits through the C0 view, which the
  commit check did not fold onto the A0 block; (2) `0x30000000 + 0x1000` repeatedly: the kernel's system
  heap (`AllocSystemHeap`: the top 256 MB of the 4 KB heap, 0x30000000-0x3FFFFFFF) had no block;
  (3) `0xFFCAE000`: the E0 view, same fold as (1). The SDK log then says `BaseHeap::Alloc failed`,
  `Unable to allocate thread object`. Fix: `fold_guest()` in the commit check, and two more blocks:
  0x30000000 (64 MB, system heap) and 0x70000000 (64 MB, guest thread stacks: `xthread.h`
  `kStackAddressRangeBegin`). Six blocks, 1088 MB of RAM.
- Every build now carries `0.<commit count>.<sha7>` (NACP display version shown by hbmenu, first log
  line, `cvhd_version.h` written by `switch/build.sh`); `switch/deploy.sh` downloads the NRO back and
  compares SHA-1; the ELF of each pushed build is kept in `/root/cvhd/elf/`.
- Added after the second run (0.32.5bbee11): `switch/host/input/nx_input_driver.cpp` (libnx pad as gamepad 0,
  positional B/A/Y/X -> A/B/X/Y, +/- Start/Back, ZL/ZR triggers, sticks; created by `input_system.cpp` on
  the Switch), and `__wrap___syscall_thread_create` runs every new thread through a trampoline that deals it
  over cores 0-2 (round robin; libnx would start them all on the creator's core 0).

### 2026-10-21: Eden runs in no-GPU mode (what the emulator caught before the console)

- 0.35: `--game_data_root was not provided`: Eden starts the NRO with **no argv at all**, so the first
  injected default became argv[0]; `main` now inserts a placeholder program name.
- 0.37: the blocks at 0x30000000 and 0x70000000 were never mapped: `MapFileView` matched blocks by the
  SDK view's start only; it now maps every block inside the requested view (0x00000000 + 1 GB holds
  0x00000000 and 0x30000000; 0x40000000 + 1008 MB holds 0x40000000 and 0x70000000).
- 0.39/0.40: the process ended silently ~0.5 s after the input setup (libnx exit path in Eden's log,
  nothing in ours): the buffered log lost the last lines. No-GPU runs are line-buffered now, the runtime
  prints flushed `[nx] runtime: ...` / `[nx] audio: ...` step marks, and `std::set_terminate` names
  uncaught exceptions.
- 0.41: `std::terminate: filesystem error: status: error [/sdmc:/switch/cvhd/game]`:
  `std::filesystem::absolute("sdmc:/...")` prepends "/" on newlib and `status()` throws. The three
  `absolute()` calls (game root, update root, user root) keep the path as given on the Switch.
- Eden's config has `cpuopt_ignore_memory_aborts = true`: a bad guest access is not reported as a crash
  there; only the console shows those.
- **0.42.deee758 passes the no-GPU gate in Eden** (4 min, `E:\conker-data\eden-runs\cvhd-0.42.deee758\`):
  every step mark through `XEX loaded` and `entering the UI loop`, `Function table initialized`, the guest's
  own init runs (xam ordinals 2815/2816/2827/2832 missing, as on Linux), it reaches the D3D ring-buffer setup
  (`VdSetGraphicsInterruptCallback`, `VdInitializeRingBuffer`, `VdEnableRingBufferRPtrWriteBack`: "no GPU
  emulation loaded; call ignored"), and the process stays alive with 3281 MB in use. Without a GPU the
  guest stalls there (nothing consumes the ring buffer or fires vblanks), so the gate's "first frames"
  criterion was not met by 0.42: hence the null backend below.
- **Null GPU backend** (0.43, `switch/sdk-edits.py`: `src/graphics/null/graphics_system.cpp` in the
  xenos plugin, `--gpu_backend=null`, a new `gpu_backend` cvar in `rex_app.cpp`, "any" by default):
  the SDK's command processor runs as it is (ring buffer parsed, read pointer written back, the vsync
  worker fires the guest's interrupt callback, XE_SWAP handled) but `IssueDraw`/`IssueCopy` do
  nothing, `LoadShader` returns null, there is no provider or presenter. The log gets
  `[nx] gpu: register write #1..5` (proves the guest's MMIO stores reach the command processor: the
  0x7FC8xxxx registers depend on codegen's compile-time `REX_MM_STORE` detection; a missed one lands
  in physical memory through the 0x7F000000 fold) and `[nx] gpu: swap #N` (1-3, then every 60th):
  that line is the Eden pass criterion. The Switch `nogpu` file selects this backend; on Linux
  `scripts/linux-run.sh 120 --gpu_backend=null` tests it without a renderer.
- Pitfall: `rsync --delete` from the Windows checkout into `/root/cvhd/src` must exclude `out`,
  `game`, `generated` and `.git`; without `generated` it clobbers the codegen output (the Windows
  checkout holds only `generated/rexglue.cmake`) and the Linux link fails on `PPCImageConfig`.
  Fix: rerun `rexglue codegen cvhd_manifest.toml` (54 s, "1 written, 266 unchanged").
- Pitfall: `switch/sdk-edits.py` edits must be written in their final form. A corrective edit that
  rewrites a previous insert breaks idempotency: the first edit no longer finds its `new`, finds its
  `old`, and inserts again on every run (0.43: `rex_nx_mark` twice in the pch; the presenter and
  input includes grew by one block per run). `git -C <sdk> diff --stat` after two runs must be equal.
- **Linux, null backend** (`scripts/linux-run.sh 120 --gpu_backend=null`): register writes #1-5
  (0xA31, 0xA2F, 0xA30, 0xD02, 0xA02), swap #1 at ~5 s, swap #720 at 120 s: the game runs its
  logic and swaps without a renderer. Same 27 optional-file `NtCreateFile` failures as with Vulkan.
- **Eden 0.48.76d7a52 (null backend): no swap, the fold was missing in `GuestPtr`.** The command
  processor got the same 5 register writes and `SetInterruptCallback(826278D8, 4002AC00)`, then
  nothing for 3 min. Eden's log showed 10 unmapped accesses at host `virtual_base + 0xFFCAB000`
  (reads at +0x18, +1, +0x10, an exclusive store at +0x10, writes of `'REX'` at +8 and a handle at
  +0xC): `XObject::GetNativeObject` initialising a KEVENT that lives in the E0 system heap
  (`SystemHeapAlloc(kSystemHeapPhysical)` allocates top-down from 0xFFCAF000: the 0.29 console log
  showed the same pages). The pointer came in unfolded: every kernel export gets its pointer
  arguments through `rex::memory::GuestPtr(base, addr)` (`hook.h`, `ppc/function.h`) =
  `base + addr + detail::PhysicalHostOffset(addr)`, and that offset had only the Windows/mac 4 KB
  branch. `sdk-edits.py` gives it the Switch fold (signed now). On the console this would have been a
  data abort right after the XEX start; Eden's `cpuopt_ignore_memory_aborts` turned it into a hang.
  The register writes themselves prove that codegen's compile-time MMIO detection works for the
  guest's GPU register stores (the first worry of the null backend).
- `switch/eden-run.ps1 [-Minutes N]` is the Eden procedure as a script: stages the NRO from WSL,
  starts Eden, prints the swap count every 10 s, closes politely, backs the logs up, prints PASS
  (>= 60 swaps) or FAIL. Another port's Eden from another folder may keep running.
- **Eden 0.49 (fold fixed): swap #1, then a stall; 0.51 marks: the host clock was 0 Hz.** The
  vsync-worker line `[nx] gpu: vsync worker: 60.0 Hz, guest ticks 50000000/s, host ticks 0/s`:
  `Clock::host_tick_frequency_platform()` (`clock_posix.cpp`) divides by
  `clock_getres(CLOCK_MONOTONIC_RAW)`'s resolution, and libnx's `__syscall_clock_getres` does not
  serve clock 5 (the asserts are compiled out in Release), so the frequency was 0, the guest tick
  count never advanced, and no vblank (interrupt source 0) was ever dispatched: only the two
  source-1 interrupts from the command stream around swap #1. Every time-based wait in the kernel
  (`KeDelayExecutionThread`, timers, `KeQueryPerformanceCounter`) had the same dead clock, so the
  console would have hung the same way. The Switch host clock is `armGetSystemTick()` at 19.2 MHz
  now (`sdk-edits.py`, `clock_posix.cpp`).
- **Eden PASS: 0.54.6f58089 (5460 swaps in 3 min, ~30 swaps/s, `host ticks 19200000/s`, 0 unmapped
  accesses, clean exit) and 0.55.9cb1472 (2640 swaps in 2 min; the rex log is 41 lines instead of
  15,796: `SetActiveCpu` no longer warns per vblank on the Switch).** Both interrupt sources run:
  source 0 (vblank, 60 Hz from the vsync worker) and source 1 (the command stream's INTERRUPT
  packet, one per swap); the guest's callback 0x826278D8 returns every time. The game logic runs on
  the null backend; the Vulkan/NVK path is untested until the console runs 0.55.
- Watch item: `BaseHeap::TranslateRelative` (`membase_ + heap_base_ + host_address_offset_ + rel`) is
  the last unfolded host path (the 0xCD fill on release, the host-page protect calls in
  `xmemory.cpp`). Nothing hit it in Eden; a console data abort at `virtual_base + 0xC0..`/`0xE0..`
  points there.

### 2026-10-22: console run of 0.55.9cb1472: the Vulkan side comes up, the game data was missing

- The log (`cvhd_log.txt`, fetched over FTP once the console came back): clocks set, NVK instance and
  device, `[nx] creating the VI surface (allowed types 0x20)`, `VulkanPresenter: Created 1280x720
  swapchain with format 44, color space 0, presentation mode 0`, the arena mapped, the pad up,
  `Vulkan sparse binding is not used ... a full 512 MB buffer will be created` (it was created), the
  function table initialized, then **`Entrypoint XEX not found: sdmc:/switch/cvhd/game/default.xex`**.
  `sdmc:/switch/cvhd/game/` was empty: the game data had never been copied (CLAUDE.md said "not
  confirmed on the SD" since the first push). `lftp mirror -R` of `game/` (359 MB, 2161 files, no
  `default.xexp`) fixes it; `switch/deploy.sh` still pushes the NRO only.
- After the failed init the shutdown crashed at pc 0 in `~VulkanInstance()`: the Switch loader
  block took `vkDestroyInstance` from `vk_icdGetInstanceProcAddr(nullptr, ...)`, and an ICD answers
  null for an instance-level function without an instance. It is loaded with the instance handle
  now, next to the other instance functions. Every normal exit would have crashed the same way.
- **hbmenu showed "cvhd" by "Unspecified Author", 1.0.0 for every build**: devkitPro's
  `nx_create_nro` takes `NACP`/`ICON`/`ROMFS` only; the `NAME AUTHOR VERSION` arguments were
  silently dropped and a default NACP generated. `CMakeLists.txt` now runs `nx_generate_nacp`
  with the git version and hands the file to `nx_create_nro`; `tools/nro_nacp.py` reads the NACP
  back out of the NRO (asset section), `switch/build.sh` prints it and `switch/deploy.sh` refuses
  a push whose NACP version is not `cvhd_version.h`'s. The log's first line was always right.

### 2026-10-22: console run of 0.58.77a4fae: first graphics (Konami logo after ~60 s, ~1 fps)

- Timeline from `cvhd_rex_log.txt` (local time 14:36): 05.5 arena mapped, 07.5 function table,
  08.2 `SetInterruptCallback` (guest running), 09.3-10.4 the first 12 graphics pipelines (NVK
  compiles them in ~1 s total: pipelines are **not** the 60 s), 10.5 "Skipping Vulkan frame
  presentation due to async placeholder draw usage" (once), then the guest's loading phase: the 25
  optional-file probes (`NtCreateFile` of `*_idx.dds` / `*_uv.uvb`) span 14:36:09 to 14:36:59 on the
  console and 1.5 s on Linux x86. The 60 s of black screen is the game's own loading code running
  30-50 times slower than on the PC, not shader or pipeline work. Memory stays at 3281 MB (the heap
  is reserved up front; not a signal).
- Frame rate: vblank interrupts (source 0) run at 60 Hz (#5400 at ~90 s), but the command stream's
  swap interrupt (source 1) never reached #300 in 85 s: under 3.5 swaps per second. No fps line
  existed in the Vulkan path yet (0.60 adds `[nx] fps ...` per second and the debug overlay).
- Next measurement before any optimisation: the per-thread CPU profile line (`[nx] cpu: name N% |`,
  0.62, `switch_nx.cpp`: `svcGetInfo(ThreadTickCount)` for every thread the create wrap registered,
  names via `pthread_setname_np`). It separates the guest's main thread (recompiled PPC code and
  its memory fold/dirty-mark overhead), the "GPU Commands" thread (Xenos emulation + NVK driver
  CPU), the vsync worker, audio and file I/O.
- Suspects for the slow guest, to be confirmed by that line: the fold compare chain and dirty-page
  byte store on every guest memory access; file reads through the bounce buffer in small chunks
  on libnx fs; three cores shared by six host threads (round robin, libnx default priorities);
  the global critical region taken for every interrupt dispatch at 60 Hz.
- **Measurement tools (0.63.e9216f5):** `[nx] fps N, frame M ms mean, K ms max (F frames)` once per
  second from the swap packet (`command_processor.cpp`, needs the perf counters:
  `switch/build.sh` configures `-DREXGLUE_ENABLE_PERF_COUNTERS=ON`; it was OFF, so the first
  attempt printed nothing), the SDK's ImGui debug overlay at start (cvar `debug_overlay`, default
  on for the Switch; `sdmc:/switch/cvhd/cvhd.toml` with `debug_overlay = false` turns it off), and
  `[nx] cpu: name N% | ...` every 2 s (`switch_nx.cpp`). Eden shows the fps line (24 fps on the null
  backend: Eden's own JIT speed, not the console's) and an empty cpu line (Eden's
  `svcGetInfo(ThreadTickCount)` returns no ticks); the console run decides whether the cpu line is
  usable there.

### 2026-10-22: console run of 0.63.e9216f5: the 256 MB block got no RAM, then a teardown crash

- Log: title takeover (no applet warning, 3285 MB), clocks set, the NVK presenter up, then
  `[nx_memory] no RAM for the 0x0 block (256 MB)`: `aligned_alloc(0x1000, 256 MB)` from libnx's heap
  returned null, where 0.58 had mapped all six blocks (1088 MB) on the same console. "3281 of 3285 MB
  used at start" is normal: libnx hands the whole region to malloc, UsedMemorySize counts it.
- Crash report `01792673432_05c838df22834000.log` (User Break, svcBreak from libnx's exception
  handler): `Runtime::Setup` returned on the failed memory init, `~RuntimeConfig` destroyed the
  graphics system, `~Presenter` freed its UI-drawer multimap and `free()` faulted at offset 0x18
  (a dangling or corrupted chunk). Our handler had reported the same `_free_r` fault first.
- Not determined yet: why malloc had no 256 MB. New in 0.63 before memory init: the ImGui debug
  overlay dialog created at start (registered in the presenter's drawer list, which is where the
  teardown then faulted) and the perf counters (no large buffers). 0.65 prints
  `[nx_memory] heap before the blocks: region R MB, malloc arena A MB, in use U MB, free F MB, top T
  MB; process P of Q MB` (newlib `mallinfo`, `svcGetInfo`) before the first block and again at a
  failure, then exits cleanly with `[nx] fatal: not enough RAM for the guest memory; exiting`
  instead of running the SDK's teardown. The overlay is opt-in again (`cvhd.toml`:
  `debug_overlay = true`). The next console log decides: if the arena is small or fragmented the
  cause is on our side (allocation order), if free space is there the allocator (newlib
  `_memalign_r` with a 4 KB alignment) or a corrupted heap is.
- Eden 0.65: 4680 swaps in 2 min, fps line 44 fps (Eden's own speed), heap line present.

### 2026-10-22: console run of 0.65.70a6533: the first numbers (2.1 fps, the guest thread is the limit)

- `[nx_memory] heap before the blocks: region 8192 MB, malloc arena 59 MB, in use 58 MB, free 0 MB,
  top 0 MB; process 3281 of 3285 MB`: newlib's arena grows by sbrk inside libnx's 3281 MB heap, the
  six blocks mapped. Why 0.63's `aligned_alloc(256 MB)` failed on the same console is still open
  (not reproduced; the line is there for the next time).
- `[nx] fps 2.1, frame 478 ms mean, 484 ms max (3 frames)` every second, constant to the millisecond
  across 70 s: a fixed per-frame cost or a timed wait, not a load that varies.
- `[nx] cpu:` every 2 s: `Main XThread` 80-115% of a core (the guest's main thread, the recompiled
  code plus the kernel calls it makes), `Audio Worker` 49% (the SDK's audio worker with the nop
  backend: it only polls `WaitAny` with a 500 ms timeout, yet burns half a core: the multi-object
  wait in `threading_switch.cpp` is a 1 ms poll loop), `GPU Commands` 1-5% (Xenos emulation and the
  NVK driver together: the GPU side is idle), everything else under 2%.
- So the limiter is the guest main thread, not NVK, not shader compilation (12 pipelines in 1 s),
  not memory size. What inside that thread: 0.67's PC sampler (`[nx] pc Main XThread (N samples):
  +0xOFFSET P% | ...`, `svcSetThreadActivity` + `svcGetThreadContext3` at 100 Hz; addr2line on
  `/root/cvhd/elf/cvhd-<version>.elf` names the bucket) decides between recompiled game code, a
  kernel wait that spins, the memory fold helpers and the MMIO check.
- **PC sampler (0.67, `switch_nx.cpp`)**: a thread at priority 0x1C pauses the "Main XThread" and
  "Audio Worker" threads 100 times a second (`svcSetThreadActivity`, `svcGetThreadContext3`, both
  allowed under hbl) and counts the PC per 256-byte bucket; `[nx] pc <thread> (N samples): +0xOFF P%
  | ...` every 2 s. Symbolize with `aarch64-none-elf-addr2line -f -C -e /root/cvhd/elf/cvhd-<v>.elf
  0xOFF`. A thread blocked in the kernel samples at the libnx svc stubs (+0x605Bxxx in 0.67/0.69:
  `svcWaitSynchronization` / `svcSleepThread` and neighbours), so "svc stub" = waiting, not burning.
- **Multi-object waits polled (fixed in 0.69, `sdk-edits.py` rule for `threading_switch.cpp`)**:
  Eden's sampler showed the audio worker at `mutexLock` 17%, `pthread_mutex_lock`, the malloc lock,
  `WaitMultiple` and its vector: the SDK's `PosixConditionBase::WaitMultiple` try-locks every handle,
  checks, and sleeps 1 ms, a thousand times a second; on the console that was 49% of a core for a
  worker with nothing to do, and every `KeWaitForMultipleObjects` of the guest pays the same. Now
  every signal also notifies one shared condition variable and the waiter sleeps on it (20 ms cap
  against a lost wake-up). Eden 0.69: the audio worker samples 90% in the svc stubs (blocked),
  `mutexLock` 2%; 4500 swaps in 2 min.

### 2026-10-22: the pathology hunt (the user's point: Conker runs at 47-60 fps on this console)

- **The 50 s boot is the VFS, not the game.** `HostPathDevice::ResolvePath`: a path missing from
  the in-memory tree gets an exact-name `stat` (fast), then `ListFiles(directory)` = `opendir` +
  one `stat` per entry (one fs IPC each on libnx: seconds for a directory with hundreds of files),
  on **every** miss. The game probes 25 optional files, 2 s each. `sdmc:` is FAT (case-insensitive),
  so the exact-name stat was already the final answer: the Switch build returns "not found" there
  (0.74, `sdk-edits.py`, `host_path_device.cpp`). Successful opens come from the tree built at
  mount (`PopulateEntry`) and were never the cost.
- **Generated code on aarch64** (objdump of `sub_8263DA60` in the 0.69 ELF): the fold is an inline
  compare chain, byteswap is one `rev`, but GCC did not inline the `static inline` helpers:
  `bl rex_nx_mark` 346 times and `bl rex_nx_host_offset` 310 times per 256 KB of code, i.e. a call
  per guest store. 0.75 marks both `__attribute__((always_inline))` (pch edits; codegen's stamp
  ignores template changes: delete `generated/default/cvhd_pch.h` to regenerate). A 2-4x class
  cost at most, not the 50x.
- `REX_CHECK_GLOBAL_LOCK` (a global recursive mutex per `mfmsr`-class instruction) appears 288
  times in 10 generated files (~3,800 in all); `mftb` 12 times. If the sampler shows the main thread
  in `global_critical_region::mutex` / `pthread_mutex_lock`, that is the next thing to attack.
- **Memory micro-benchmark** (`switch/host/membench_nx.cpp`, at boot, `[nx] membench ...` lines):
  malloc memory vs a 4 KB-aligned and a 2 MB-aligned code-memory block, with `svcQueryMemory`
  type/attr/perm. Eden's numbers are the PC's (meaningless); the console's decide the cache
  hypothesis. Attr 0x8 would mean uncached.
- Research input (`C:\Users\Administrator\AppData\Local\Temp\x360\FINDINGS_PERF.md`, Rayman
  Origins on ReXGlue runs 60 fps on phones): the manifest sets none of the register-localisation
  flags (`skip_lr`, `skip_msr`, `ctr/xer/reserved/cr/non_argument/non_volatile_as_local`;
  UnleashedRecomp sets all eight) = a 1.5-3x class win with a smaller .text; the swapchain must not
  be recreated on VK_SUBOPTIMAL_KHR; two frames in flight. Order: sampler verdict first, then the
  flags (a full codegen + both builds, ~40 min).
- **Existing ReXGlue Switch ports** (research session, clones in
  `C:\Users\Administrator\AppData\Local\Temp\x360\`: `sa2-nx` Sonic Adventure 2 at 60 fps with a deko3d
  renderer, `usf4-nx` Ultra Street Fighter IV at 60 fps, GPL-3: ideas and measurements only, no files).
  Facts taken from `sa2-nx/sdk/src/core/guest_memory_switch.h` and `docs/`:
  - True aliases exist on Horizon: `memalign` backing, `svcMapProcessCodeMemory` makes a code alias,
    `svcSetProcessMemoryPermission` RW on it, then `svcMapProcessMemory` mirrors **that alias** into
    every view (heap memory as the source is what fails with 0xD401/0xE401, which is what our probe
    tried). PhysicalHostOffset is 0 there. Candidate for later: drop the software fold.
  - The game's "wait for GPU" is a spin (32x `db16cyc`, a no-op in codegen): the guest main thread
    sits at ~100% of a core at any fps. SA2 made it sleep on the ring's progress counter (game
    specific). So our 100% main thread is expected; the limiter is what the command processor delivers.
  - Scheduling: only priority 0x3B is time-sliced (10 ms); `svcSleepThread(0)` yields only to the same
    priority; `thread_local` is a function call (`__aarch64_read_tp`). Guest 16 MB stacks fragment the
    heap: pool them. Thread handles leak without a reaper. SD opens are slow: an asset pack.
  - Codegen flags they ship: cr/ctr/xer/reserved/non_volatile/non_argument_as_local, skip_msr,
    elide_lr_stores, `[rexcrt]` host memcpy/memset, LTO + PGO (~15%), -O3. Their CPU rounds: fewer
    `msr fpcr` mode switches, constants folded from read-only data, guarded direct calls for
    single-target indirect calls, loop registers as locals. Our generated code: 1 `msr fpcr` per
    420 instructions (theirs 1 per 185), 1 `bl` per 36 instructions (theirs 1 per 130; ours mostly
    `rex_nx_mark`/`rex_nx_host_offset`, inlined since 0.75).
- **Thread placement (0.76, `switch_nx.cpp` `place_thread`, off with `sdmc:/switch/cvhd/nopin`):**
  the mechanism that fits a constant 478 ms frame: the spinning guest and the command processor's
  1 ms WAIT_REG_MEM polls at the same, non-time-sliced priority on one core, so every poll waits for
  a scheduling quantum. Now: "Main XThread" alone on core 0 at 0x3B, "GPU Commands" at 0x28 on core
  1 (preempts any guest thread at once), "GPU VSync" 0x27 and audio/XMA on core 2, other guest
  `XThread*` time-sliced at 0x3B on cores 1-2. The cpu line prints each thread's priority (`pXX`),
  `[nx] gpu waits: N sleeping polls (M ms asked), K yield polls` counts the command processor's
  WAIT_REG_MEM polls, and `[nx] sleepbench` measures a 1 ms sleep through svcSleepThread,
  nanosleep and std::sleep_for at boot.
- **The blind sleep in WAIT_REG_MEM (0.79):** the stock command processor answers an unmet
  WAIT_REG_MEM (the packet the game uses to wait for a vblank or fence value) with
  `Sleep(wait / 0x100 ms)` per poll, `wait` from the game's packet, while the guest spins on the
  result; the research note (FINDINGS_PERF.md section 8; Hard Corps: Uprising's PC port patched the
  same class: wake on the guest word, wait for the vblank instead of a blind sleep) ranks this first
  for a constant per-frame time with an idle GPU thread. 0.79 counts the polls and the milliseconds
  asked (`[nx] gpu waits`) and caps the sleep at 1 ms on the Switch; the console log shows both the
  original ask and the fps with the cap. Eden 0.79: 3480 swaps in 2 min, the placement lines show
  every thread on its core and priority, a 1 ms sleep takes 1.5 ms there.

### 2026-10-22: console run of 0.79.7ac37a5: two hypotheses dead, the block failure reproduced

- `[nx] sleepbench: 1 ms sleep takes svcSleepThread 1.00 ms, nanosleep 1.00 ms, std::sleep_for
  1.00 ms; sched_yield 0.2 us; main thread priority 0x2C`: sleeps are exact. Not the clock, not the
  sleep.
- `[nx] membench malloc: write 10223 MB/s, read 4062 MB/s, dependent load 140.7 ns (type 0x5, attr
  0x0, perm 0x3)`; code memory 4 KB aligned: write 10217, read 3735 MB/s, 146.9 ns (type 0x15, attr
  0x0); 2 MB aligned: 10245 / 4214 MB/s, 136.0 ns. The guest arena's memory is cached and as fast as
  malloc memory (the 140 ns is DRAM latency: the chase defeats every cache on purpose). Not the
  cache attributes.
- `[nx] memory: 3189 MB, 3185 MB used at start` (this launch had a 3189 MB pool; others 3285):
  "used" is libnx's heap, which takes the pool at start. Then `heap before the blocks: malloc
  arena 62 MB, in use 61 MB` and **`no RAM for the 0x0 block (256 MB)`** with `heap at the failure:
  arena 321 MB, in use 321 MB`: newlib's allocator grew the arena by the 256 MB and still returned
  null (its extend-top path gives up when the second, aligning sbrk does not fit its expectation),
  exactly as 0.63. 0.65, which allocated the blocks before any other heap activity, mapped all
  1088 MB. 0.81 therefore takes one 1088 MB pool as the program's first allocation
  (`rex_nx_reserve_pool`, 2 MB aligned, blocks carved from it) and prints the launch type
  (`[nx] launch: application (title takeover)` or an APPLET warning) and the heap bounds.
- The fps/cpu/pc/waits lines of 0.79 never ran (the process exited at the block failure); the
  placement and the capped WAIT_REG_MEM sleep are still unmeasured.

### 2026-10-22: console run of 0.82.8bfe6a8: the pool mapped, then NVK got no memory

- `[nx] launch: application (title takeover)`, `memory: pool 3285 MB, 3281 MB used at start (the
  heap itself: 3165 MB)`: libnx's heap is the whole pool minus the NRO; "used" = that heap, not
  usage. `heap before the arena pool: arena 0 MB` then `arena pool: 1088 MB` (one aligned_alloc
  from the empty arena worked), all six blocks mapped.
- Right after, at the swapchain: `nvkmd-switch: allocate memory failed: out of host memory
  (VK_ERROR_OUT_OF_HOST_MEMORY)` (`nvkmd_switch_dev.c:142`), `VulkanPresenter: Failed to create a
  swapchain`, and `heap before the blocks: malloc arena 2282 MB, in use 2282 MB`: newlib's malloc
  refused NVK's allocations while growing its arena by 1.2 GB. The same allocator behaviour as the
  256 MB failures, now triggered by the 1088 MB chunk sitting in the arena. The ImGui overlay's
  fonts are small (ProggyTiny + Japanese 12 pt), not the 1.2 GB.
- 0.84: the pool is carved from the **top** of libnx's heap by lowering `fake_heap_end` (newlib's
  sbrk stops there), so malloc never holds a giant chunk; a malloc self-test (256 MB + 1 MB) and
  heap lines before/after prove the allocator's state in the log. The overlay is opt-in again
  (`cvhd.toml`: `debug_overlay = true`) until its cost is measured with those lines.
- Still unmeasured on hardware: fps/placement/sampler/WAIT_REG_MEM counts (every run since 0.65
  died before the game).

### 2026-10-22: console run of 0.84.9e829f2: the game runs again; the sampler names the loop

- Memory: pool carved, `malloc self-test: 256 MB ok, 1 MB ok`, arena 0 MB after the pool, every
  block mapped, NVK swapchain fine. The heap carve is the fix for the whole 0.63/0.79/0.82 family.
- Still 2.1 fps, 478 ms per frame, with the placement on (cpu line: `Main XThread 96% p3B`,
  `Audio Worker 49% p2A`, `GPU Commands` 1-14% p28). The thread placement changed nothing.
- **Sampler, guest main thread** (200 samples/2 s): `+0x369500..+0x369900` ~45% = `sub_82620BD0`
  (+ `CRRegister::compare` inlined), `+0x3BFB500` 8% = `sub_826275C0` (its caller),
  `+0x15F2300` 6% = `sub_827484A0`, `+0x1163300` = `__restgprlr_29`. `sub_82620BD0` is the game's
  D3D "wait for the GPU": 32x `db16cyc`, then it reads a word through a pointer in the device struct
  (`*(dev+10896)`, a 96-byte physical block from `sub_8263B678(96, ...)` that the game hands to
  `VdEnableRingBufferRPtrWriteBack`: the ring's read-pointer writeback), compares it with the
  position it waits for, and after 5000 units without progress calls `sub_826309A8` (a `DbgPrint`
  and a recovery). So the guest waits for the command processor to consume its ring.
- **Sampler, GPU Commands**: 95% in the svc stubs (a 256-byte bucket; 0.86 prints the exact PC).
  `[nx] gpu waits: 35-60 sleeping polls (35-60 ms asked)` per second: 17 WAIT_REG_MEM polls per
  frame at ~28 ms each = the 478 ms, if those polls are what paces the frame. The Vulkan command
  processor does nothing around a poll (`PrepareForWait`/`ReturnFromWait` are empty), so either
  the 1 ms sleep takes 28 ms on that thread, or the command processor idles between the guest's
  kicks. 0.86 measures the real sleep time and probes the folded GPU register window (physical
  0xC80710/0xC80714 = CP_RB_RPTR/WPTR): a guest store to a register through a pointer that codegen
  did not recognise as MMIO lands there on the Switch (no page faults) and the command processor
  never sees it. If that word moves, the fix is a range check in the pch's store/load path.
- During loading the main thread sits 92% in the svc stubs: the boot's black screen is SD file
  I/O (libnx fs IPC per open/read), the class sa2-nx solved with an asset pack. Later.
- Audio worker: 49% of a core, 72% of its samples in the svc stubs, with nothing to do: the
  shared condition variable wakes it on every signal of every wait object. 0.86 lets the worker
  leave at once on the Switch (no audio backend).

### 2026-10-22: the root-cause hunt, step by step (after the user's reset: one cause, numbers first)

- **Step 1, the guest loop.** `sub_826275C0(dev, target, ...)` = D3D's "wait for the ring": it reads
  the ring read-pointer writeback word (`*(dev+10896)`, a 96-byte physical block given to
  `VdEnableRingBufferRPtrWriteBack`) and spins (via `sub_82620BD0`: 32x `db16cyc`, a check of the
  device's thread id `*(KTHREAD+0x14C)` and a 5000-tick budget on `*(KTHREAD+0x58)` = the 360's
  `KernelTime`, which our kernel never advances, so that timeout never fires) until the GPU has
  consumed the ring past `target`. It waits for the command processor, nothing else. The kick
  (CP_RB_WPTR, register 0x1C5) is a compile-time-recognised MMIO store (`sub_82626D68`,
  `REX_MM_STORE_U32(r10 + 1812)`); Eden at 30 fps proves the kicks arrive. The E0 fold (+0x1000)
  matches the SDK's view table (file offset 0x100001000) and `PhysicalHeap::GetPhysicalAddress`.
- **Step 2, the Linux reference (lavapipe), `cp/s` lines from the SDK edit:** 2-6 swaps per
  second there too, with 6 kicks, ~1.3 batches and 23 WAIT_REG_MEM packets per frame; the guest
  main thread at 79% (the same spin), the command processor not among the busy threads. So the
  slowness is not Horizon's: both Vulkan builds show it, the null backend (Eden, 30 fps) does not.
  The WAIT_REG_MEM packets are `reg 0xA31 (COHER_STATUS_HOST) mask 0x80000000` (the D3D
  coherency flush, satisfied at once), fences in a 96-byte block (`mem 0x1FC9B0xx`), and
  `reg 0x1973 (AVIVO_D1SCL_UPDATE)`; all "satisfied after 0 ms". The polls sum to 10-50 ms/s.
  **`cp/s time: batches 1153 ms (draws 238 in 1143 ms, swaps 0 ms), idle waits 0 ms`**: the
  command processor spends its whole second inside `IssueDraw`, 4.8 ms per draw, 119 draws per
  frame, with the thread mostly blocked (low CPU). The console: 478 ms per frame over the same
  draw count = 4 ms per draw. The same per-draw cost on a software rasterizer and on a hardware
  GPU means a synchronisation per draw, not rendering work. The phase timers inside the Vulkan
  `IssueDraw` (`draw phases ms: pre/prim/shaders/textures/rt/pipeline/state/bind+vb+draw`) name
  the step.
- **Step 3, Hard Corps: Uprising's PC port (GPL-2, ideas only):** its SDK patches 0003 (idle
  wait + WAIT_REG_MEM polls capped at 1 ms: the same as ours), 0019 (the idle event wait made
  non-alertable: an alertable POSIX wait polls in 1 ms slices), 0025 (polls wait on a
  guest-progress event set by the vblank interrupt), 0009 (host threads blocked on a guest word
  woken when the command processor writes it: futex / WakeByAddress), 0006 (timers and waits
  sleep instead of spinning), 0021 (WaitMultiple contention). None of them is a 50x item; they
  remove CPU waste and latency around waits.
- Dead ends closed with numbers: sleeps (1.00 ms), cache attributes (10 GB/s, attr 0), MMIO
  kicks (recognised; probe), the fold (verified), the host clock (19.2 MHz), the EDRAM path (host
  render targets on both), memory (pool carved).

### 2026-10-22: THE ROOT CAUSE (with numbers): the GPU work the Xenia backend generates per frame

- Linux, `draw phases ms`: every millisecond of the command processor's draw time is in the "pre"
  step, and `pre sub ms` splits it: `BeginSubmission 1040 ms (calls 242, opening frames 2, new
  submissions 2, fence waits 0 in 1040 ms, texture begin 0, prim begin 0)`: the time is the fence
  poll of `CheckSubmissionFenceAndDeviceLoss`, run only when a frame opens, i.e. **the command
  processor waits for the previous frame's GPU work before it opens the next**, ~520 ms per frame.
  gdb on the "GPU Commands" thread confirms it: `CheckSubmissionFenceAndDeviceLoss <-
  BeginSubmission <- IssueDraw`, blocked inside lavapipe. The guest's 100% spin and the console's
  idle command thread are symptoms of the same wait (on NVK the wait is an nvdrv fence).
- Linux, `gpu work/s` at 8 swaps/s: `rt transfer passes 888 (128 transfers, 32 resolve clears),
  texture loads 24 (104192 KB), shared memory uploads 16456 KB, resolves 96`. Per frame:
  **111 render-target transfer passes** (the host-render-target EDRAM emulation moving EDRAM
  contents between host render targets at every render target switch: 16 switches per frame),
  12 resolves (the 360's predicated tiling: the frame is rendered in EDRAM tiles and resolved
  tile by tile), 4 resolve clears, 3 texture reloads of ~4 MB each (13 MB per frame of textures
  rewritten by the game every frame) and 2 MB of vertex/index uploads. The null backend does none
  of this: Eden 30 fps. lavapipe needs ~500 ms per frame for it (8 threads); the Tegra X1 GPU
  through NVK needs ~478 ms (constant, the same scene every frame). That is the 2.1 fps.
- Why 50x worse than a PC: a desktop GPU absorbs ~110 full-screen passes per frame in a few
  milliseconds; a 768 MHz Maxwell with 25 GB/s of shared memory bandwidth does not, and NVK's
  render pass handling adds to it. sa2-nx/usf4-nx reached 60 fps only with a renderer of their
  own ("~70% of the GPU is EDRAM emulation" even then). Hard Corps: Uprising's PC patches do not
  touch this class.
- Not the cause (each closed with a number): sleeps, clock, cache attributes, MMIO, the fold,
  memory, thread placement, WAIT_REG_MEM polls, the file system (that was the boot).
- Next, in order: (1) A/B the other EDRAM path on the console without a build: `sdmc:/switch/cvhd/
  cvhd.toml` with `render_target_path_vulkan = "fsi"` (fragment shader interlock, NVK supports
  it: one EDRAM buffer, no ownership transfers; resolves stay); (2) GPU timestamps per frame on the
  console to put a number on the GPU side; (3) decide between cutting transfers/resolves in the
  backend (predicated-tiling awareness, as usf4-nx did) and a native 2D renderer for this game.

### 2026-10-22: the native renderer, step by step (Eden gate; no console run)

Directive: the game must run with a picture in Eden before anything more goes to the console. Eden cannot
run NVK, so the renderer is deko3d (the devkitPro example `deko_basic` renders in Eden at 60 fps:
`E:\conker-data\eden-runs\deko_basic\shot-02.png`; `switch/eden-shot.ps1 -Nro <nro> -Out <dir>` takes the
screenshots).

- **Shape (advisor-reviewed):** a new GPU backend behind the SDK's Xenos command processor
  (`--gpu_backend=deko3d`, `src/gpu/deko3d_backend.cpp`, compiled into the SDK's plugin through
  `REXGPU_XENOS_EXTRA_SOURCES` + the weak `rex_gpu_extra_backend` hook in `switch/sdk-edits.py`), not Direct3D
  hooks: the ring, fences, WAIT_REG_MEM, read-pointer write-back, vblank, PM4 parsing, the register file, the
  shader analysis (`Shader::vertex_bindings/texture_bindings`) and the texture layout code stay the SDK's; only the
  drawing is new. The Vulkan backend stays the default and builds unchanged. No EDRAM model: a host image per
  render target keyed by EDRAM base and pitch, resolves are image copies into images keyed by the destination's
  guest address, a texture fetched at such an address aliases the image (no readback, no reload), the swap draws
  the image at the front buffer's address into the nwindow swapchain with one triangle.
- **Census (M1, `tools/census_gen.py`, `CVHD_CENSUS=ON`):** XDK 2.0.9328 (the XEX import-library versions; the
  same Direct3D as Futari, whose published device layout holds here: +48/+52/+56 command buffer, +1920 VS
  constants, +10548 the 0x2200 shadow, +10940 flags, +11992 declaration, +12612 index buffer, +12616..+12632
  render targets/depth, +12704 stream stride, +12868/+12872 PS/VS). `sub_82608C98` is the game's render-list
  executor and the only caller of the four draws (`sub_82624FB8` DrawVertices 6/frame at the title,
  `sub_826253A8/826257F8/82625C70` the indexed and UP variants), Clear `sub_8262A140`, Resolve `sub_8262AAF8`,
  Swap `sub_826287A8`; `docs/native-renderer-plan.md` section 6 has the table. Not needed by the backend shape
  chosen, kept as the map of the game.
- **Trace backend (M3a, `--gpu_backend=trace`, Linux and Switch):** logs every distinct draw/resolve/swap state
  once (`[trace]` lines in the SDK log; `scripts/linux-run.sh 100 --gpu_backend=trace --dump_shaders=<dir>`). The
  title frame: the XDK clears a surface in two draws (a 2x-MSAA alias at half the pitch covers most of it, rect
  lists cover the remainder; stride-7 vertices: xyz + rgba), the scene is a 1024x600 target (EDRAM base 0, pitch
  1040, depth at tile 494) resolved to two textures (1D5E0000 / 1D120000, 1024x608 aligned), an upscale rect
  draws it into the 1920x1080 back buffer (base 0, pitch 1920, no depth), sprites are single quads
  (`kQuadList`, 12-byte vertices from the inline ring at vf95), four full-frame resolves copy the back buffer
  into textures and the front buffers (1EAA0000 / 1F298000; XE_SWAP shows 1920x1080), plus "point n=1 mask 0"
  dummy draws (GPU fences). Depth resolves (to 1D840000 / 1D380000, k_24_8_FLOAT) exist; ignored for now.
- **Shaders (M2, `tools/xenos2glsl.py`):** Xenos ucode -> GLSL from the SDK's own disassembly dumps, semantics
  from the SDK's interpreter; uam compiles all 11 title-screen shaders (vertex inputs one per vfetch in
  instruction order, constants in uniform binding 0, the viewport/alpha-test system constants in binding 1,
  textures by fetch slot, the pc-loop for jumps). Shader names are the FNV-1a hash of the ucode
  (`src/gpu/ucode_hash.h`), shared by the trace dumps, the DKSH files and the backend's lookups. Missing shaders
  are dumped to `sdmc:/switch/cvhd/shaders/missing/`.
- **Staging:** `switch/build.sh` writes `out/build/switch/stage/` (NRO, `shaders/*.dksh` from the trace dumps +
  the present pass, the `deko3d` marker file); `switch/eden-deko3d.ps1` copies it to Eden's SD and runs with
  screenshots. The marker `sdmc:/switch/cvhd/deko3d` selects the backend (wins over `nogpu`).
- **sdk-edits idempotency (found the hard way):** an insert that a later edit modifies is re-applied on every
  rerun (duplicate `CpStats`, `nx_phase_ticks`). Rule: every insert is written in its final form; a later edit
  never touches an earlier insert's text. `python3 switch/sdk-edits.py <sdk>` twice must change nothing.

### 2026-10-22: M3 reached: the title screen drawn by the deko3d backend in Eden (0.104.a11a55b+)

- Screenshot: `E:\conker-data\eden-runs\cvhd-m3-title	itle-0.104.png` (Eden window, the full title artwork,
  logo and copyright line, right side up); its log next to it. Eden's own speed: `[nx] fps 38.1, frame 26.2 ms
  mean` (the null backend ran 30-44 fps there: the backend adds no visible cost in the emulator). Per title frame:
  6 draws, 10 clears, 10 resolves, 25 images (131 MB of the 224 MB image pool), 49 KB of per-frame data.
- Procedure: `switch/build.sh` (stages NRO + DKSH + the `deko3d` marker in `out/build/switch/stage/`), then
  `powershell -File switch/eden-deko3d.ps1 -Seconds 90 -Every 15` (stages into Eden's SD, screenshots of every
  Eden window, logs backed up to `E:\conker-data\eden-runs\cvhd-<version>-deko3d\`).
- Bugs on the way, each with its number:
  1. The static plugin kept the weak `rex_gpu_extra_backend` default and never linked the game's backend
     object ("requested backend 'deko3d' is not compiled"): the hook is a required symbol now.
  2. `TextureInfo` keeps the fetch constant's size-minus-one fields: a 1x1 texture became 0x0 and the GPU
     hung after 3 swaps (`dkFenceWait` never returned). All sizes are `info.width + 1`.
  3. The XDK's fast clear is a **4x MSAA alias at half the pitch** (2x2 samples per pixel: 960x536 alias
     pixels = 1920x1072), not 2x: `rt pitch 960 msaa 2` in the trace means `MsaaSamples::k4X` (= 2).
  4. A uniform brown frame (then white at the logo): the translator marked `tfetch2D r0, r0.xy`'s result
     written before it saw the read of r0, so no pixel shader declared its texcoord input and every textured
     draw sampled texel (0,0) (`dump at swap #600: front ... 1 distinct colour FF247092`). Operands are parsed
     before results now (fetches and ALU). The image readback dump (`[dk] dump at swap #120/#600`, five pixels
     and the distinct colours of a 16x16 grid per image) found it without screenshots.
  5. Upside down: deko3d with the upper-left origin maps NDC y = +1 to the bottom row (Vulkan-like); the Xenos
     clip space is y-up. `GetHostViewportInfo(..., origin_bottom_left = true, ...)` negates the NDC y
     scale/offset the shader applies.
  6. Resolve destinations are created at the aligned size (1920x1088, 1024x608) and a fetch/swap asks for the
     exact size: a resize copy once, and a resolve then accepts any image at least as large (the first
     version resized back and forth every frame and leaked pool memory).
- The Konami logo (10 s in Eden, `E:\conker-data\eden-runs\cvhd-m3-title\logo-partial-0.105.png`): the
  logo renders, but only the top of its white backdrop is right and the rest is stale texture memory. The
  backend uploads a texture once, at the first draw that binds it, and the game fills (or replaces) that
  memory afterwards. M4 item: re-upload when the content changes (a sparse signature of the guest bytes at
  bind time, or the Switch dirty-page bytes). The title art (DXT1 1920x1080, RGBA8 1000x416 and 646x40) is
  complete.
- "Press START" is missing from the title: its draw (quads, VS 9DC6ADB3 + a 312-dword PS B3EC630B) samples a
  `k_16` 576x48 glyph strip (swizzle A00) and a 4096x1 RGBA8 lookup texture; `k_16` is R16_Unorm since 0.105
  (it was refused before), the text is still invisible: the text shader's translation or the 16-bit sampling
  (unorm vs integer) is the M4 suspect.
- Not covered yet (M4): depth resolves (ignored), `cull_front/cull_back` (culling is off until a picture shows the winding),
  stencil, textures the game rewrites after the first upload (no dirty tracking: `rex_nx_dirty` is the hook),
  the `k_1_5_5_5`/`k_5_6_5` channel order, 1D/3D/cube textures (2D only), indexed draws (code path untested),
  the title's "Press START" text (not in the screenshot: blink phase or the `k_16` texture), input in Eden to
  reach the menu and the first stage, and the console run (clocks, real fps, the SD shader directory).

### 2026-10-23: M4 in Eden: "Press START" drawn, textures follow memory writes, keys reach the game (0.107-0.108)

- **"Press the START button" (fixed, `E:\conker-data\eden-runs\cvhd-m4\title-press-start-0.107.png`)**: two
  translator faults, found by reading the generated GLSL of PS B3EC630B32280C8C, not by trial. (1) The write
  tracking was per register: `r0.z = ...` before `tfetch2D r0, r0.xy` marked r0 written, so no `i_0` interpolator
  input was declared and the texcoord was (0,0) (the same class as the M3 texcoord bug, one level finer).
  `tools/xenos2glsl.py` tracks written **components** now: a read of an unwritten component makes the register an
  input. (2) The shader samples its 4096x1 colour lookup with `tfetch1D`; the translator declared `sampler1D`
  while the backend bound a 2D image (the SDK's `TextureInfo::Prepare` turns k1D fetch constants into 2D of
  height 1, as the Xenos has no 1D textures). 1D fetches are `sampler2D` with `vec2(u, 0.5)` now (Xenia does the
  same). The glyph strip is `k_16` (R16_Unorm) and it is rewritten by the game: the re-upload lines show the
  576x48 strips changing (the text pulses).
- **Textures follow guest memory (no more stale uploads)**: the backend consumes the Switch dirty-page bytes
  (`rex_nx_dirty`, nothing else reads them without the SDK's SharedMemory): before every draw and resolve it
  stamps each dirty 4 KB page with a draw serial; an image remembers the serial of the upload or resolve that
  filled it and is uploaded again at its next bind when any of its pages is newer. Host writes that bypass the
  store macros are marked too: the SDK's `XFile::Read` bounce copy calls `rex_nx_mark_range` (sdk-edits,
  `memory_nx.cpp`). An image whose memory is reused with another format is recreated (the old pool memory is not
  reclaimed: bump pool). At the static title: 0 re-uploads per frame, 1 per ~7 s (the text strip).
- **Missing-shader loop**: `Shader::DumpUcode` goes through `std::filesystem::absolute` (broken on newlib,
  finding 0.41), so the backend writes the two dump files with stdio into `<shader dir>/missing/`;
  `switch/eden-deko3d.ps1` copies them into `/root/cvhd/shaders_trace/` after each run and `switch/build.sh`
  stages them. First catch: VS 7B7C787BDB5CE03E (33 dwords) appears right after START is pressed.
- **Input in Eden**: `switch/eden-shot.ps1 -Keys "40:M,55:C"` presses keys at seconds (Eden's keyboard map in
  `user\config\qt-config.ini`: C = A, X = B, V = X, Z = Y, M = +, N = -, WASD = left stick, arrows = d-pad).
  `SetForegroundWindow` is refused from a background process, so the key is posted to the game window's queue
  (`WM_KEYDOWN/UP`); Qt delivers it and the game took START (the text disappeared, the next screen's shader
  was requested). The shader directory is a cvar (`--deko3d_shader_dir`), the debug markers live next to it;
  `docs/native-renderer-plan.md` section 8 lists what another port needs to reuse the backend.
- Interference: during run keys1 somebody opened Eden's menu and its Controls settings (not the script: the
  dialog appeared before the first key) and Eden was closed at 68 s with the config written; treat a run's
  early exit as external when the game log ends without a crash line. The person was using the PC: the
  screenshots (CopyFromScreen of the window's screen area) then captured another application's window on
  top of Eden. `eden-shot.ps1` captures with `PrintWindow` now (the window's own content only) and posts
  keys with `PostMessage` (no `SetForegroundWindow`): a run no longer touches what the person is doing.
- **Rectangle lists in Eden (found through the per-draw-type log and its vertex dump, 0.109)**: after the
  title the game draws a full-screen black fade (VS 7B7C787BDB5CE03E + the pass-through PS, a 3-vertex
  rectangle list at the corners (0,0) (1920,0) (0,1080), alpha 17/255 at its first draw, then opaque) and
  only its first triangle appeared: Eden ignores deko3d's `fillRectangleEnable` (NV_fill_rectangle). Nothing
  had proved the mode before: the title art is a quad, and the one other rectangle draw (the 1024x600 scene
  upscale) draws a black scene at the title. `tools/xenos2glsl.py` now writes a pass-through geometry shader
  per vertex shader (`shader_<hash>.geom.dksh`: 3 vertices in, a 4-vertex strip out with v3 = v1 + v2 - v0
  for the position and every interpolator, the Xenos rectangle semantics); the backend binds it for
  rectangle-list draws; the cvar `deko3d_fill_rectangle=true` selects the hardware mode instead (the
  console supports it; compare there). `dkCmdBufBindShaders` is called with the whole graphics mask so a
  stage without a shader is unbound.
- **The fade is the attract transition, not START**: it begins 25-35 s after the title on its own (the
  `draw VS 7B7C787B` line comes before the START key in every run) and never on Linux in 100 s (Linux has
  no connected pad: the SDL driver finds none). Right before it the game opens the directory
  `game:\data\player_dll\` (its character DLLs) and that failed with STATUS_NO_SUCH_FILE: (1)
  `VirtualFileSystem::OpenFile` split "data\player_dll\" into a parent and an empty name; (2) libnx cannot
  `open()` a directory. sdk-edits: trailing separators stripped in OpenFile (every platform), and on the
  Switch a directory entry opens as a `HostPathFile` without a host handle (Read/Write/SetLength refuse).
  With the fix the game continues: it probes the character files `data\opd\player\<name>\p_*_idx.dds` /
  `*_uv.uvb` (optional, as the 25 title probes) and the fade stays opaque while it loads.
- The dirty-page counter per frame (`N dirty pages` in the swap line): 9211 pages written before the first
  frame, 10-25 per frame at the title, 44 when the text strip changes. The Konami logo frames use the
  1024x600 scene and the upscale rectangle: their look is to be rechecked with the geometry shader.

### 2026-10-23: the main menu in Eden (0.112): the audio driver was the blocker, pad.txt is the input

- **Stuck behind the fade = no audio driver.** With the kernel import trace on (`sdmc:/switch/cvhd/args.txt`
  lines `--log_noisy=true` and `--log_level=trace`; the SDK rotates the log at 5 MB into `cvhd_rex_log.1.txt`)
  the loader thread's last call was a physical allocation, then every game thread sat in
  `svcWaitProcessWideKeyAtomic` (the sampler now covers the game's six other threads: `[nx] pc XThread...`).
  `XMACreateContext` came right at the fade, and the SDK's nop audio system returned NOT_IMPLEMENTED from
  `CreateDriver`, so `XAudioRegisterRenderDriverClient` had failed at boot and the game's audio engine never
  got a render callback; the transition waited on it. sdk-edits: the nop system creates a driver that
  "plays" each submitted frame (256 samples at 48 kHz = 5.33 ms) by releasing the client's semaphore, and the
  audio worker stays (0.86 had made it leave). The game then loads for ~35 s at Eden's speed and opens its
  main menu (`E:\conker-data\eden-runs\cvhd-m4\menu-main-0.112.png`: the book with Single Player ... Exit
  Game; `menu-single-player-0.112.png`: the Single Player page after A, the page-turn effect samples the
  previous frame through the resolve images). 17-23 fps in Eden under load; 61 draws per frame.
- **Input: `sdmc:/switch/cvhd/pad.txt`**, read by the Switch pad driver (`nx_input_driver.cpp`): lines
  `<seconds>:<BUTTONS>[:<hold ms>]` (START BACK A B X Y LB RB LT RT UP DOWN LEFT RIGHT LS RS, chords with
  `+`), seconds from the driver's start, 200 ms hold; `eden-deko3d.ps1 -Keys "38:START,100:A"` writes it.
  Posted keyboard messages reached the game only sometimes (Qt's focus widget); this needs no focus and works
  on the console too. START at 38 s -> fade at 73 s -> menu at 77 s; A at 100 s -> the Single Player page.
- The 4 characters probed at the fade (tpf, tpm, syanoa, soma) are the player data; the `player_dll\*.dll`
  listing ends with STATUS_NO_MORE_FILES after the entries (normal); no DLL is loaded by the menu.
- Open at the menu: text glyphs lose thin strokes (the sampler took the magnification filter for
  minification: fixed in 0.113, 16 samplers), uninitialized image memory shows in parts of the frame (the
  background layer: under investigation with the per-texture log), the character card is garbled.

### 2026-10-23: cloud session: the deko3d backend runs on Linux, the first stage renders and the player moves

- **Linux test host for the deko3d backend** (`src/gpu/dk_gl/`): the backend compiles unchanged on Linux against an
  OpenGL stand-in for libdeko3d (headless EGL + Mesa llvmpipe, GL 4.5). Shaders are the translator's GLSL
  (`shader_<hash>.vert|frag|geom.glsl`), frames and uploaded textures are PPM files. It reproduced the Eden
  pictures (the six card backs, the book) and found the faults below without Eden. `docs/LINUX-TEST-HOST.md`,
  `docs/PROGRESS-cloud.md`. Speed: ~1.5 swaps/s on 4 cores; `CVHD_DK_START_SWAP=N` and the file
  `<dump dir>/skip_on` turn the backend into a null backend (the guest runs at 60 swaps/s) to reach a scene.
- **Pitch**: `TextureInfo::pitch` is in texels, `Untile` wants tile-aligned blocks: DXT textures were untiled with a
  4x row pitch (everything below the first 32 block rows wrong). Now `info.extent.block_pitch_h`. The Guardian
  Heroes port found the same bug independently (`docs/from-gh/`).
- **Samplers**: filters come from the tfetch instruction unless it says "use the fetch constant" (the glyph
  strips were point-sampled), all eight clamp modes are mapped as in the SDK's Vulkan backend, border colour
  white/black. 512 sampler descriptors.
- **Formats**: deko3d names packed formats MSB first (`format_traits.inc`): RGB565/RGB5A1/RGBA4 have R in the low
  bits, Xenos has R in the high bits: BGR565/BGR5A1 for 5-6-5 and 1-5-5-5, 4-4-4-4 through a reversed view
  swizzle. BC4/BC5, RG16F, RG32F, RGBA32F, RGBA16 added.
- **The six identical cards are correct**: empty player slots show the card back (an atlas entry at the top left
  of the DXT3 336x832 texture). **Noise at the edges**: new render targets and resolve images start cleared.
- **Character DLLs** (`data/player_dll/dll*.dll`, XEX modules, one loaded per character; the default is Soma at
  0x83090000): the manifest has them as `[[modules]]` (six DLLs recompiled in 6 s each). The SDK loads them as shared
  libraries by the bare name `cvhd_dllSoma` (dlopen cannot find `libcvhd_dllSoma.so` without the `lib` prefix: fixed in
  `kernel_state.cpp`). **Function tables**: the SDK puts a module's dispatch table right after its image; the main
  module's (0x82960000 + 13.8 MB) covers the DLL images ("Module image range overlaps existing"), so with DLL
  modules the codegen emits `REX_TABLE_BASE` (main table 0x80100000, a DLL's 0x81000000 + 2*(code_base-0x83000000)),
  carried by `PPCImageInfo.table_base`. **Static modules for the Switch** (no dlopen): each module's objects are
  joined with `ld -r` and every global but its two renamed entry points is made local with `objcopy` (the modules
  define identical helper symbols: `__imp____savegprlr_14`, `PPCFuncMappings`); the generated registry hands the
  function pointers to the kernel state. Tested on Linux with `-DCVHD_STATIC_MODULES=ON`. All in `switch/sdk-edits.py`
  (blocks "Guest DLL modules" and "Function tables out of the way"); a template edit needs
  `rexglue codegen --ignore-stamp` with the CLI the game build made.
- **The stage**: after "Start the Game" the DLL loads and the first stage runs (Linux, trace backend: 570 draws per
  frame, 60 swaps/s). The backdrop layers are 4320x2432 RGBA8 textures (41 MB each): the 24 MB staging ring could not
  hold one ("staging ring full": its draws sampled nothing, the stage was black but for the HUD). Staging is 72 MB.
  With it the whole stage draws (castle rooms, HUD, timer) and holding `d` moves the player. Paletted sprites
  (R16 index texture + 4096x1 palette through PS B3EC630B) draw correctly.
- Still open: the staging ring is per frame (2 x 72 MB): a 41 MB texture uploads in the frame it fits, the next
  one the frame after; a chunked or NEON untile for the console; depth resolves, culling, stencil are unverified
  because this stage shows no need yet.

### 2026-10-24: the Switch build with the six DLL modules (0.130-0.135): five link fixes, the Eden gate reaches the first stage

- Task: the latest build on the console. The cloud session's static-module link (`REXGLUE_STATIC_MODULES`,
  sdk-edits "Guest DLL modules") had been tested on Linux x86-64 only; devkitA64 needed five commits:
  1. 711d25d: `ld -r` of a module choked on GCC's precompiled header `cmake_pch.hxx.gch` (the object filter
     knew clang's `.pch` only).
  2. b5279a3: the filter `[.](pch|gch)$` broke the link: parentheses inside the generator expression were split
     into linker arguments ("cannot find ("). A character class, `[.][pg]ch$`, has no parentheses.
  3. b784fd9: `objcopy --keep-global-symbol` of only the two entry points also localized the weak COMDAT
     inlines and templates from the SDK headers; the final link deduplicated the groups and the local copies
     pointed into discarded sections ("`FPSCRPlatform::GuestToHost` defined in discarded section").
     `tools/module_keep_symbols.py` builds the keep list from `nm`: weak definitions stay global.
  4. 81313c2: the keep-list argument carried literal quotes under VERBATIM (objcopy: cannot open '"...keep"').
  5. 61f78db, the 0.134 gate failure: `DEFINE_REX_FUNC` makes every guest function a **weak alias**
     (`sub_X` -> `__imp__sub_X`, so a hook can replace it). The keep list of fix 3 kept all 813+ aliases
     global, and the final link resolved the main module's `xstart` (the XEX entry 0x8263BA90),
     `__savegprlr_N` and `__restgprlr_N` to the Alucard DLL's copies (first in link order). The guest's main
     thread ran the DLL's CRT start against unloaded memory and ended at once: `Execution complete` 11 ms
     after `Initializing shader storage`, 0 swaps; the sampler's one sample of the main thread sat in
     `sub_830227C0` (Alucard). Rule now: a weak symbol with an `__imp__` twin in the same object is a guest
     function alias and is localized; the keep list shrank from 863 to 13 names per module. Check:
     `nm cvhd.elf | grep " [A-Za-z] xstart$"` shows one `W` (`generated/default`) and six `t`.
- Codegen templates live inside the host CLI: after a template edit in `sdk-edits.py`, rebuild `rexglue`
  (`cmake --build <sdk>/out/build/linux-amd64 --config Release --target rexglue`), then
  `rexglue codegen cvhd_manifest.toml --ignore-stamp` (a plain rerun writes 0 files).
- The 16 stage shaders were collected on Linux with the trace backend driven by log events, not by swap
  counts (`scripts/linux-drive.py` steps `waitlog 'ps=B3EC630B32280C8C'` for the title, the shader hashes of
  the menu, `code=83` for the DLL load): the cloud's swap-number timings pressed keys before the menu existed.
- Eden gate 0.135.61f78db (own Eden copy `E:\Switch\Eden-cvhd\`, logs and 24 screenshots in
  `E:\conker-data\eden-runs\cvhd-0.135.61f78db-deko3d-gate2\`, pad.txt `38:START, 100:A, 130/134/138/142:DOWN,
  148:A`): the main menu at 45 s, the Single Player page at 105 s, the page-turn "Loading" at 150 s,
  `Registering module: data/player_dll/dllsoma.dll (code_base=830A0000)` and `Function table initialized ...
  code=830A0000-830BDFF4, image=83090000-83110000` at 158 s, the stage overview at 210 s (sky and sprites, no
  castle rooms: the first bind of a 4320x2432 backdrop logged `staging ring full: 41040 KB texture not uploaded`
  at 168 s; the rooms are drawn by 345 s; the frame of that upload is not logged), the castle rooms with HUD,
  timer and the player at 345 s. 3540 swaps in 360 s; stage frames 451-453 draws, 10 clears, 7 resolves, 0 missing shaders (26 DKSH), 255 MB of images;
  4-7 fps at Eden's speed. The exit-path `std::terminate: uncaught exception: error` seen in 0.134 (after
  `UI loop ended`, when the main thread had ended) is unverified: the gate closes Eden before the guest exits.
- Honest note: the first smoke run of the own Eden copy still used the user's `E:\Switch\Eden\user` paths
  (absolute paths in the copied `qt-config.ini`) and wrote `cvhd_log.txt` / `cvhd_rex_log.txt` into the
  user's `sdmc\switch\cvhd\` once (2026-10-24 14:37); the copy's config points at `Eden-cvhd` now.
- Pushed to the console (2026-10-24, `push.sh`: curl upload + SHA-1 read-back of every file): `cvhd.nro`
  0.135.61f78db (200,923,333 bytes, SHA-1 a6e14130cc5c524634a274088cf401bbdc145c2d), the `deko3d` marker and
  26 `shaders/*.dksh`; `nogpu`, `pad.txt`, `args.txt` and the `dk_*` debug markers removed. The console's
  `cvhd.toml` (`render_target_path_vulkan = "fsi"`) is left as it was (the deko3d backend ignores it). No
  console run yet: that is the user's next step (hold R on a game for title takeover, "Castlevania HD" by
  cvhd-recomp in hbmenu; Switch B = OK, A = Back, + = START; logs `sdmc:/switch/cvhd/cvhd_log.txt` and
  `cvhd_rex_log.txt`).

### 2026-10-27: the first console run of the deko3d backend (0.135) and what 0.141 adds

- **Console run of 0.135.61f78db** (user's run, log fetched by FTP, 114 s): clocks 1785/768, pool and arena fine, the
  deko3d backend up with all present and stage shaders, no crash, no missing shader draws, one `staging ring full`
  (a 41 MB backdrop). `[nx] fps` averages: title 41 fps (15 s window), main menu 23 fps, stage 7.5 fps (12 fps at
  the first stage frames, 5 fps once the stage is loaded: 430 draws, 7 resolves, 255 MB of images per frame).
  CPU lines in the stage: GPU Commands 25-28% (p28), Main XThread 3-16%, the six other guest threads 9-16% each,
  **Audio Worker 73-91%** (the nop audio driver wakes it for every 5.33 ms frame: a wasted core). The PC sampler puts
  the Main XThread (79%) and the GPU Commands thread (84-87%) in `svcArbitrateLock`: both wait on a mutex, not on
  work. Next measurement: who holds the lock (the global critical region, the command processor's lock, the
  frame data ring), before touching rendering.
- **0.141.04c5aba**: (1) on-screen fps counter, top-left, drawn by the present pass (swaps per second over one
  second; 3x5 digits in `present.frag` from a 512-byte uniform). The integer/bit-operation version made Eden's shader
  recompiler die (exception code 0x20474343, first frame that drew it), so the shader is float-only and branch-free.
  (2) Sound: `switch/host/audio/nx_audout_driver.h` (a copy of the Guardian Heroes port's libnx audout driver, same
  author; 48 kHz stereo s16, 2x1024 samples, device-paced, mute file `sdmc:/switch/cvhd/mute`) used by the nop audio
  system on the Switch (`sdk-edits.py`). Eden: audout opens, ~5500 frames consumed, 0 dropped, peak 0.06, 36
  underruns in the first minute. Not yet heard on hardware.
- Pushed (SHA-1 read back): `cvhd.nro` 280f06dfb6a93a882b6b0e5bdf29d7763da27a8c (200,931,525 bytes), `deko3d`, 26 shaders.
  The 0.140 push failed halfway (the console's FTP was shared with another session's upload and reset the
  connection); 0.141 replaced it.
- Open from the user's report, not analysed: pages "flip" mirrored/inverted (the book's page-turn samples the
  previous frame; the Eden screenshot of 0.135 shows the same mirrored "Loading"), graphic artifacts, low fps.

### 2026-10-28: console 0.141 numbers, the book page text, a wait that spun (0.143-0.147)

- **Console 0.141** (user's run): loading screens ~12 fps, stage 7.5-10 fps ("cannot be played"), the book's left page
  and the "Loading." label mirrored left-right (screenshots in `D:\git\dev\console-shots\2026-10-28\cvhd\`). The stage
  overview and the first frames show small sprites in green boxes (paletted sprites scaled down; not analysed yet).
  `[nx] cpu:` in the stage: Main XThread 3-5%, GPU Commands 25-27%, six other guest threads 2-16%, Audio Worker 65-73%.
  The 0.135 "svcArbitrateLock" reading was a symbol mix-up: in the 0.141 ELF the hot stub is
  `svcWaitProcessWideKeyAtomic` (a condition-variable wait), so the threads WAIT for something, they do not compute.
- **Book text (0.146, Eden):** the page-turn mesh is a quad list (prim 13, VS D5A8AAF7/9DC6ADB3) that the game draws with
  face culling (front pass and back pass, mirrored texture on the back); the backend never culled. Now
  `PA_SU_SC_MODE_CNTL` cull_front/cull_back and `face` map to deko3d for triangle and quad primitives (cvar
  `deko3d_cull`: 0 off, 1 as the guest asks, 2 inverted winding; 1 is right in Eden: mid-turn "Loading..." reads
  correctly). Culling of triangle lists alone (0.145) changed nothing: the page mesh is quads.
- **A wait that spun (found by a counter, 0.144-0.146):** `WaitMultiple` in `threading_switch.cpp` cast the time left to
  whole milliseconds, so the last millisecond of every timed multi-object wait was `wait_for(0)` in a loop. Counter
  `[nx] multi waits: N calls, M ended by the 20 ms cap`: Eden 0.144: 3000 calls and 520,000 "timeouts" per 2 s; 0.146
  (absolute deadline `wait_until`): 900. Also fixed in 0.144: a lost wake-up (check, then wait: a signal in between
  cost up to 20 ms): a generation counter plus a pass through the mutex before the notify. This is the likely cause of the
  Audio Worker's 65-73% of a core. Timed condition waits themselves are exact (`[nx] condbench`: 20.6 ms for a 20 ms wait).
- **Tools added:** the PC sampler logs, for threads in a lock/condvar wait, the mutex word, the owner thread and four
  return addresses (`[nx] lockwait ...`); `[nx] dk time/s: draws+resolves X ms, acquire Y ms, fence wait Z ms over N
  frames` per second from the backend (a frame's commands are recorded into one buffer and submitted at the swap; two
  frames in flight; a large fence wait means the GPU is the limit).

### 2026-10-28 (evening): why the console was slow, and what 0.150-0.163 changed

- **Wait profile (0.150, `[nx] wait ...` every 2 s):** per wait object and thread, the time blocked in
  `PosixConditionBase::Wait` and who signals the object. Eden stage: the main thread is blocked 87% of the time on
  one event signalled by four worker threads; those four hand one guest critical section (a mutex made from an
  event) to each other ~3500 times a second, each hand-off a condition-variable sleep and wake (~285 us), so
  the workers run one at a time. Console 0.144: 32-39% of the workers' PC samples sat in `rmutexLock <-
  pthread_mutex_lock <- sub_825EBAF8` (the game's mtmsrd critical sections = the global critical region) and
  in malloc's lock; `svcArbitrateLock` hands a contended mutex to the waiter inside the kernel, so on cores shared by
  time-sliced 0x3B threads the lock waits for the new owner's time slice.
- **Fixes (0.152):** (1) recursive mutexes (`std::recursive_mutex`, newlib's malloc lock) spin briefly and then sleep 20 us
  (`--wrap=rmutexLock/TryLock/Unlock` in `switch_compat.c`; file `sdmc:/switch/cvhd/nospin` restores libnx's);
  (2) a plain wait polls its object for 30 us before it sleeps (file `waitspin_us`; Eden: 62% of those polls hit);
  (3) alertable waits round the time left UP to a millisecond: they cut it to 0 and looped `Wait(0)` (the command
  processor's 5 ms idle wait made 59,000 waits per 2 s, now 1,250); same bug in the multi-object wait (0.146: 520,000
  spins per 2 s, now 900); (4) a lost wake-up in the multi-object wait (generation counter).
  File `allcores` lets the guest's other threads run on core 0 too (the main thread uses 3-5% of it).
- **Address fold by a 256-entry table (0.159, from hcu-switch):** `.nro` 200 MB -> 92 MB; Eden stage mean fps 6.7 -> 10.8.
  A template edit: `sdk-edits.py`, rebuild `rexglue`, `codegen --ignore-stamp` (`scratchpad/regen.sh`).
- **dcbz128 did not mark its page dirty (0.160)**: the codegen builder for `dcbz` with a 128-byte line used
  `REX_RAW_ADDR` (77 sites in the main module). Fixed, but the noisy platform strips of the pipe room stayed: they are
  not (only) that.
- **Book text (0.146):** face culling (`PA_SU_SC_MODE_CNTL`) for triangle and quad primitives; the page-turn mesh is a
  quad list drawn front and back. Confirmed in Eden: "Loading..." and the settled pages read correctly.
- **Frame trace (0.153-0.163, file `sdmc:/switch/cvhd/dk_trace`, content = seconds after backend start):** one line per draw
  of the first 440+ draw frame after that time, PPM dumps of the front buffer, the targets and every guest image
  (`dump/NN.ppm`, 1_5_5_5, 5_6_5 and R16 decoded), decoded screen corners of sprite quads. 1_5_5_5 textures decode
  correctly (the minimap and a heart sprite look right), so the noisy platforms are not the format.
- **Open:** the noisy ledge strips (a 16x88 index texture of a dithered gradient: the palette bank or the palette texture
  are the suspects), the green grid left of the HP/MP bars, the console fps with these fixes (0.152+ not yet run
  there).

### 2026-10-29: Eden stage 11 -> 21 fps, the noisy sprites and HUD digits found (0.170-0.177)

Eden stage numbers: mean fps of the last 40% of `[nx] fps` lines (the stage, 460-512 draws per frame), same pad.txt
(`38:START,100:A,130-142:DOWN x4,148:A`), 330 s. Runs while another session compiled in WSL (load 20) are not valid:
the title screen dropped from 38 to 33 fps too (0.171). Check `uptime` in WSL before a run.

| Build | Change | Eden stage fps |
|---|---|---|
| 0.166 | baseline | 11.2 |
| 0.170 | `Signal`/`Release` notify after the object mutex is released; a semaphore acquire no longer wakes every waiter | 20.2 |
| 0.177 | constants copied per used register run; 96 MB staging ring; resized/packed textures fixed | 21.0 |

- **The lock convoy (0.170, `sdk-edits.py`, `threading_switch.cpp` rules):** the SDK's `PosixCondition<Event>::Signal`,
  `<Semaphore>::Release` and `<Timer>::Signal` called `notify_all` while holding the object mutex, so every woken waiter
  blocked again on that mutex (`svcArbitrateLock`): Eden 0.166 put the main thread 64% of its samples on the reacquire.
  The semaphore's `post_execution` (an acquire) also did `notify_all` + a multi-wait wake although the count only goes
  down. Now: state change under the lock, notify after it; `notify_one` for a release of 1. The wait profile changed
  shape: the four workers no longer hand a critical section to each other; they wait for the main thread's jobs (~40%),
  the main thread waits for them (~59%).
- **Constant copies (0.171, 0.175):** the per-draw copy of 2 x 4 KB float constants was 30% of the GPU Commands
  thread's samples (sampler now records memcpy callers like lock waits: `mutex` = source, `owner` = destination).
  A span (lowest to highest used register) was still 4 KB: the sprite PS reads c3-c4 and c254-c255. Now each run of
  used registers (`Shader::constant_register_map().float_bitmap`; dynamic indexing sets every bit). memcpy left the
  thread's profile; the samples moved to `dkCmdBufBindUniformBuffers` (Eden's JIT reports block starts, so this is
  coarse).
- **Noisy sprites, part 1 (0.174):** a guest address reused for a texture of another size (the game's sprite pool:
  20-30 size changes per address in a stage, 1137 in one run) went through the resolve-destination resize: the old
  image's top-left texels were copied and kept as current content. A tiled texture of another size has another
  layout, so this was noise that no write ever replaced. Uploaded textures are now uploaded again; resolve
  destinations keep the copy. A barrier precedes every upload (a recycled image may still be sampled in the frame).
- **Noisy sprites, part 2 = the HUD digits and the ledge strips (0.177):** the HP/MP digits (draws #443-448 of the
  0.163 trace, `k_16` 16x24 index textures) and the 16x88 ledge strips have a side of 16 texels or less, so their base
  level lives in the **packed mip tail** at an offset inside the 32x32 tile (`texture_util::GetPackedMipOffset`: x 16
  for a texture taller than wide). The untiler read from 0,0. `UntileFast` takes a block origin now (tests:
  175 of 175 equal to the SDK's `Untile` with `offset_x/offset_y`). Eden: "303 / 100" readable, the pipe-room
  platforms drawn. The dark-green dotted panel behind the digits remains: probably the game's own panel (not checked
  against the original).
- **Staging ring 96 MB (0.172):** both 41 MB stage layers fit in the first stage frame; no `staging ring full` line
  in 0.174/0.177 runs (72 MB had one per stage start).
- Where the frame goes now (Eden 0.177, stage, 47 ms per frame): GPU Commands 611 ms/s in draws (28 ms per frame, 35%
  of its samples waiting); main thread 59% waiting for the four workers; the workers ~60% busy each in guest code.
  So the recompiled guest code on the workers and the command thread's per-draw cost share the limit.
- Next levers: (1) console run of 0.177 (`[nx] cpu`, `[nx] wait`, fps); on the console the four workers and GPU
  Commands share cores 1-2 while the main thread waits 59% on core 0: try `sdmc:/switch/cvhd/allcores`;
  (2) codegen register-localisation flags (`skip_msr`, `cr/ctr/xer/reserved/non_volatile/non_argument_as_local`) for
  the guest code; (3) fewer per-draw deko3d commands (sys constants bound once per change).
- Pushed (2026-10-29, `push.sh`, SHA-1 read back): `cvhd.nro` 0.177.e175c40 (SHA-1 3e3bbab8fa3a61b15538fbfbad9a2175db9ef6f8), the `deko3d` marker, 26 shaders. 0.170 never reached the console (FTP offline). No console run yet.

### 2026-10-29: console 0.177 (22 fps in the stage), the map (textures without a base level), the scheduler fixes (0.181-0.183)

- **Console 0.177** (user's run, log `E:\conker-data\hub-logs\cvhd-0.177-console\`): menus 30-60 fps, loading 30, stage
  22-24 fps (42 ms per frame), Change Weapon menu 20, the full-screen map 24 with the castle layers missing and boxed
  sprites. `[nx] dk time/s: draws+resolves ~300 ms, fence wait 0 ms` (the GPU is not the limit: 12 ms per frame of
  command recording), `cp/s: waitregmem 557 packets 215 polls 216 ms, idle waits 400 ms` (the command processor sleeps
  9 ms per frame in 1 ms WAIT_REG_MEM polls: fences in the ring's 96-byte write-back block at 0x1FC9B0xx and
  `COHER_STATUS_HOST`, whose "satisfied after 1-35 ms" is the `MakeCoherent` dirty-page flush). `[nx] cpu`: Main XThread
  7%, GPU Commands 36%, the four workers 22-27% each, core 0 93% idle (the main thread waits 80% of its time on the
  workers' job event: `[nx] wait Main XThread on 0x3e6d8c2c50: 1608 ms blocked in 96 waits`), cores 1-2 at ~130% of
  200%. The workers' samples: 30-39% in `svcSleepThread` = the rmutex waiter's 20 us sleep (`rmutex: 27,000 contended
  locks, 8,000 sleeps in 2 s`; 2.8 thread-seconds / 8,000 = ~350 us per "20 us" sleep: a woken thread queues behind
  the running time-sliced thread of its core), 5-8% in `std::mutex::lock <- rex::chrono::UpdateGuestClock` (the guest
  clock's tick mutex, one per `mftb`), and `nxprof::RecordWait`'s own mutex (the wait profile) in the lock-wait callers.
- **The map (0.181):** the full-screen map (the camera zoomed out with the right stick) and the stage overview fetch the
  4320x2432 castle layers with **base address 0 and mips 1-1 at a mip address** (the game streams the base level out and
  keeps the half-size mip): the backend uploaded from physical 0 and drew nothing. `SourceOf(info)` in
  `deko3d_backend.cpp`: a fetch without a base level takes its largest stored mip from the mip address (the SDK's
  `texture_util::GetGuestTextureLayout` offsets, pitch and packed tail); the host image is that mip at its own size
  (2160x1216). The first-bind texture line now prints `mips a-b at <mip address>`; the file `dk_trace` with the content
  `mip` traces the first frame that fetches such a texture. Eden: the 2160x1216 dump is the castle (`[dk] image file
  .../dump/05.ppm`). Not on the console yet (0.177 is there).
- **pad.txt sticks (0.183):** BACK opens the quick-chat wheel, not the map; tokens `LSUP LSDOWN LSLEFT LSRIGHT RSUP
  RSDOWN RSLEFT RSRIGHT` deflect a stick fully for the hold time (`240:RSDOWN:3000` zooms the camera out).
- **Scheduler (0.182, `switch_nx.cpp`, `switch_compat.c`):** the game's other threads run on cores 0-2 (dealt 0, 2, 1,
  mask 0x7; the file `core12` restores cores 1-2), the main thread owns core 0 at 0x3A (it preempts the time-sliced
  workers on its core when it wakes); an rmutex waiter spins 20 us, then yields with core migration
  (`svcSleepThread(-1)`) for 500 us, then sleeps 20 us (`[nx] rmutex: N contended, Y yields, S sleeps`);
  `--clock_no_scaling=true` (the guest clock without its mutex: the time scalar is 1); the wait profile and the PC
  sampler run only with the file `sdmc:/switch/cvhd/prof` (their mutexes were in the profile themselves).
  Eden cannot measure any of this (Horizon's scheduler is the subject); the console run decides.
- **skip_msr (0.183, `codegen_flags.toml`, included by the entrypoint and the six DLL modules):** the SDK emulates the
  game's interrupt-disable sections (`mtmsrd r13,1` ... `mtmsrd rN,1`, 885 sites) with ONE process-wide recursive mutex
  and every `mfmsr` (3,800 sites) takes it too (`REX_CHECK_GLOBAL_LOCK` = `global_critical_region::AcquireDirect`): the
  13,000 contended locks a second between the four workers. On the 360 those sections only mask interrupts on their own
  hardware thread, the real locks are lwarx/stwcx (a compare-and-swap here), and the host delivers no asynchronous
  interrupts into guest code; sa2-nx, usf4-nx and UnleashedRecomp ship `skip_msr`. The flag is a codegen rerun
  (`rexglue codegen cvhd_manifest.toml --ignore-stamp`, `scratchpad/chain6.sh`) plus the Switch build.
- Eden runs during another session's build are invalid (0.182: a sphaira LTO build, WSL load 16, the title at 30 fps
  instead of 38-42, the stage at a constant 20.0 fps / 50.0 ms): check `uptime` and `ps` before a run, and rerun.
- **Results (Eden, stage mean of the last 40% of the fps lines):** 0.177 21.0 → 0.181 (map fix) 23.4 → 0.184 (skip_msr +
  scheduler) **27.3** (rmutex contentions 40,000 → 7,500 per 2 s; frames mostly 33 ms = 30 fps with dips to 50 ms;
  the title 35-44 unchanged) → 0.187 27.9 (under a foreign build's load). Pushed to the console: 0.184 (SHA-1
  365522f1ef50f48ab1d1d5c860923ee55afb978a, PUSH OK) and 0.187 (phase timers, uniform cache, no per-draw strings).
- **Draw phase timers (0.186, `[nx] dk time/s ... draw phases ms: sync/setup/tex/state/vtx/uni/draw; uniform uploads
  vs/ps/sys`):** Eden 0.186: uni 305-395 of ~500 ms/s, tex 70-108, vtx 17-76, sync 23, state 12. The uniform cache
  (0.187: a float block is copied only when `WriteRegister` saw its registers written, the sys block only when its
  contents differ) cut uni to 252-266 only: the game writes the VS and PS constants before nearly every draw
  (13,000 vs and 11,700 ps uploads a second at 12,600 draws). In Eden the uni/vtx phases mostly measure Eden's
  write tracking of GPU-visible memory (a fault per written page of the frame data ring), not the console's cost:
  the console's own phase line (0.187) is the number to read.
- **The per-draw diagnostics were string work (0.186):** every draw built two snprintf keys (48 and 160 chars) and a
  `bound_textures` string and looked them up in `unordered_set<std::string>`; now numeric keys (`FirstTime(uint64)`),
  strings only for a first sighting or the trace. On the console (0.177: 27 us per draw, 12 ms per frame on the
  command thread) this is the suspected bulk; unmeasured until the 0.187 console run.
- **"The camera creeps up when zoomed with R3" (user, Eden):** not the game: the user watched the automated
  Eden-cvhd run whose pad.txt held `280:RSUP:30000` (the right stick pans the zoomed camera); `240:RS` alone zooms
  in and the camera stays on the character (0.187 run, shot 260 s). `eden-deko3d.ps1` leaves pad.txt on the SD:
  delete it before a manual session. The user's own Eden copy (`E:\Switch\Eden`) still holds 0.112 + `nogpu`.
- **WAIT_REG_MEM spin (0.189, `sdk-edits.py` rule after "changed (21)"):** a poll within the first 300 us of its packet
  spins (`yield` instructions, counted as yield polls = spin iterations now), later polls keep the 1 ms sleep. Eden:
  sleeping polls 386-390 -> 231-258 per 2 s, 131,000-143,000 spin iterations per 2 s. Built, staged, **not pushed**:
  the console decides (the spin at priority 0x28 holds core 1 for up to 300 us per poll; if the sleeping polls stay
  near 200/s there, revert). What the console runs next: (a) 0.187 as it is, 60 s in the stage: fps, `[nx] cpu`,
  `dk time/s` phases, `gpu waits`, `rmutex`; (b) the same with an empty `sdmc:/switch/cvhd/prof`: the `[nx] wait` graph
  and the PC sampler. The lever after that follows the `[nx] cpu` line: GPU Commands near 100% -> the phase line;
  a worker near 100% -> codegen flags (cr/ctr/xer/reserved_as_local first); nothing near 100% at 30 fps -> the
  main/worker hand-off latency. Frames snap to vblank multiples (33.3 / 50 ms): 60 fps needs the whole frame under
  16.7 ms on both the guest side and the command thread.

### 2026-10-29: console 0.187: menus 60, gameplay ~50 fps; where the frame goes (the prof run)

- **User's run (a), no prof:** menus 60 fps, gameplay ~50 fps without stutter, the R3 zoom ~35 fps, the camera follows,
  no artifacts. **Run (b), with `sdmc:/switch/cvhd/prof`** (log `E:\conker-data\hub-logs\cvhd-0.187-console-prof\`,
  deleted on the console afterwards): the profiling itself costs: 33-38 fps (27.5 ms frames, 450-540 draws).
- **The frame (prof run, per 2 s = 72 frames):** `[nx] cpu` sums to **262% of the three cores**: the four game workers
  31-38% each, GPU Commands 28-29%, the audio mixer thread 16-21%, the ring-kick helper 16%, Main XThread 10-11%, XMA 7-10%,
  four menu/helper threads 6-7% each, Audio Worker 5%. The guest code alone is ~220%. Main waits 1531 ms in 144 waits
  (2 per frame, 10.6 ms each) on one object the workers signal once per frame; the workers wait 472-486 ms in 72 waits
  (one job wait per frame, 6.6 ms) on the object main signals 4x per frame; the workers' samples: 53-60% in the condvar
  wait, the rest guest code. So the critical path is main (3 ms) + two worker job phases, and the workers are slowed by
  **CPU starvation** (three cores at ~87% with everything else), not by locks any more: rmutex 2,600 contended per 2 s
  (42,000-44,000 yields: holds of 100+ us, the holder is not named by the sampler), skip_msr removed the rest.
- **GPU Commands** (28%): `dk time/s: draws+resolves 175-199 ms` = **5 ms per frame** (0.177: 12 ms; the per-draw
  strings were the difference), phases tex 98-121, sync 21, uni 20, vtx 15, state 11; 73% of its samples in
  `svcSleepThread`: 600 sleeping WAIT_REG_MEM polls per 2 s (8 per frame, 1 ms each) and the idle waits; `fence wait 0`.
  Not on the critical path while the guest side is the limit. The uniform cache: 16,500 vs and 14,500 ps uploads a
  second at ~17,000 draws: the game writes the constants before nearly every draw.
- **Arithmetic for 60 fps:** total CPU per frame ~50 ms without profiling (262% x 20 ms, minus the profiler) over three
  cores; the frame must drop below 16.7 ms on the critical path and the total under ~45 ms. The lever that scales all
  guest threads is the codegen register localisation (`codegen_flags.toml`: cr/ctr/xer/reserved first, 0.190+), then
  non_volatile/non_argument with an audit; then the command thread's tex phase (3 ms per frame) and the 1 ms polls.
- **0.193 (cr/ctr/xer/reserved_as_local):** the NRO 92.2 -> 88.3 MB; Eden stage **31.4 fps** (0.184: 27.3 in a clean run;
  this run under a load of 4 with the title at 30-36 instead of 35-44, so the real gain is larger), frames mostly
  33.3 ms with some 16.7 ms ones, HUD and sprites intact. Pushed to the console (SHA-1 in the push log). It also
  carries the 0.189 WAIT_REG_MEM spin (no runtime switch yet): read `gpu waits` sleeping polls against 0.187's 300/s.
- **Step 2 audit (`tools/nonvolatile_audit.py`, run on 0.193's code):** 33 GPR candidates, all loop-continuation
  fragments of the CRT copy/fill routines (`sub_8264055C`, `sub_82640854`, `sub_826441F8`, `sub_826444B8`,
  `sub_826610D0`, `sub_82434F70`, `sub_8273A874`, `sub_827435F4`, `sub_827817D0`, and the same four per DLL at
  0x83xxxxxx): they are entered mid-loop and read the parent's counters (r22/r24/r26/r28/r30/r31), so with
  `non_volatile_as_local` they need `share_registers` (or must stay reached inline); 242 vector candidates (v14-31,
  v64-127) still to be classified (vector saves through `ea`, partial lane writes); 0 FPR. Not enabled.
- **0.196 (+ non_volatile_as_local, step 2):** `__savegprlr`/`__restgprlr` calls 29,000 -> 0, the NRO 88.3 -> 85.5 MB;
  the SEH funclets (`__finally` bodies: the parent passes its frame in r12, the funclet reads the parent's r22-r31) are
  found by the generator from the exception tables and kept on ctx with a hand-over around their calls; the audit
  (`tools/nonvolatile_audit.py`, right-hand-side reads included) lists only those funclets. Eden stage 30.9 fps vs
  0.193's 31.4: both sit on the 33.3 ms vblank step, so **Eden can no longer rank CPU gains in the stage** (a frame
  must drop under 16.7 ms to show); the title 36-40. Gate passed (picture, HUD, 0 crashes). Pushed instead of 0.193
  (which never reached the console: FTP off); `/root/cvhd/stage_0.193` keeps 0.193 for a fallback push.
- Next Eden metric for CPU work: a frame-time histogram (16.7 / 33.3 / 50 ms shares) or the wait profile with `prof`;
  the console's `[nx] cpu` sum is the real one.
- **0.200 (frame histogram in the fps line, `cpspin_us` file):** `[nx] fps N, frame M ms mean, K ms max (F frames: a <=17 ms,
  b <=34, c <=51, d longer)`. Eden stage baseline (0.200, last 40% of the run): 3,662 frames, **0-7% under 17 ms, 72%
  at 33 ms, 25% at 50 ms, 2% longer**; mean 28.6 fps (run-to-run noise +-2 at this step). The share under 17 ms is
  the Eden metric for the 60 fps work now. `sdmc:/switch/cvhd/cpspin_us` sets the WAIT_REG_MEM spin (default 300 us,
  0 = the 1 ms sleeps only, as 0.187): the console A/B needs no rebuild. **On the console (when its FTP answers):
  0.200** (SHA-1 690657cf1c807fac93d5cf44865dace390d105c8, `push.sh` waits and verifies); `/root/cvhd/stage_0.193`
  and `stage_0.196` keep the two earlier builds for a fallback push.
- `tools/nonvolatile_audit.py --nonarg` (for a step 3, `non_argument_as_local`, not enabled): r12 33 = the funclets'
  frame argument (handled by the generator), f0 read at entry by `sub_8230B2D0`, `sub_8281BC00`, `sub_827F8280`,
  `sub_82848684` (hidden float argument: MSVC interprocedural allocation), r11 by `sub_82796928`, `sub_82641A50`,
  r0 by `sub_825C2C50`; v32-v63 only in the two vector save/restore helpers `sub_82893F28` / `sub_82894270`. The
  generator has no per-function keep list for the non-argument set, so step 3 needs an SDK edit (keep f0/r11 on ctx
  for those seven, or hooks) before it is safe.
- **Step-2 audit of the localized functions themselves (`tools/locals_audit.py`: bare `rN` locals read before a write;
  the ctx-based audit cannot see them):** 30 GPR candidates in 0.196/0.200's code, 0 FPR; 26 are reads right after a
  label that only a branch reaches (the generator lays loop bodies out before their initialisation: `goto` forward,
  the body, the write), 2 are `xor rN,rN,rN` zeroing idioms (`sub_8263CC30`, `sub_8263CFC0`), 4 are one-instruction
  "functions" with no caller that the data-pointer scan registered (`sub_82745C74`, `sub_83534174`, `sub_831D5364`,
  `sub_83146454`: `lwz rA, d(rB)` then the next function). The 157 vector candidates are the same vector saves
  (`stvx vN,r11,r12` in the save helpers' callers). No hazard found; 0.200 stays queued for the console. Gap: the
  Eden gate plays Soma only; the other five DLLs were regenerated too, so one console run with another character.

### 2026-10-29 (evening): console 0.200: 40-45 fps but jerky; the frame histogram; the texture bind cache (0.206)

- **Console 0.200** (user: gameplay 40-45 fps, "jerkier" than 0.187's ~50; log read over the console MCP, FTP off,
  `E:\conker-data\hub-logs\cvhd-0.200-console\`): menus a solid 60 (54-61 of 61 frames under 17 ms); the stage: 1,536
  frames in 35 s = **56% at 17 ms, 39% at 33 ms, 4% at 50 ms** — the 17/33 ms alternation is the jerkiness (the mean
  41-45 hides it). `fence wait 54-79 ms/s` appeared (0.187: 0): some frames now wait for the GPU. `[nx] cpu` still sums
  to ~250%: GPU Commands 35-36% (the spin: 458,000 iterations per 2 s, 654 sleeping polls), the four workers 28-34%,
  main 13-15%. `dk time/s`: draws 236-251 ms, tex 138-155 (3.5 ms per frame), uniform uploads 21,000 vs + 18,000 ps per
  second, 605 draws per frame. 0.187's smooth run has no log (the prof run overwrote it): no distribution to compare.
  Percentiles at vblank quantisation ARE the buckets: p50 = 17 ms, p95 = 33 ms, p99 = 50 ms for this run.
- Jitter suspects among 0.188-0.200: the WAIT_REG_MEM spin (holds core 1 at priority 0x28 for up to 300 us per poll,
  8 polls per frame) is the only scheduling change after 0.187; the register localisation only makes the guest
  faster. A/B without a build: `sdmc:/switch/cvhd/cpspin_us` = `0` (written over the console MCP; the 0.187
  behaviour); the histogram line and `gpu waits` decide.
- **0.206 (texture bind cache, GPU time):** a per-frame cache keyed by the fetch constant's 6 dwords + shader binding
  (image, descriptor+sampler handle, memory range, size, from_resolve); a hit re-checks the dirty pages only
  (`MemoryChanged`) and binds the handle. Eden: `texture bind hits 89,000-98,000/s`, tex 138-155 -> 46-50 ms/s,
  stage 32.8 fps under a load of 12 (the best so far), picture intact (R3 zoom, HUD). GPU time per frame from
  `dkCmdBufReportCounter` (TimestampPipelineTop at the frame's first command, Timestamp before the present; the
  16-byte report's second u64 is the timestamp): `gpu N ms/M frames` in the dk time line (Eden's counter is not
  real: 0.1 ms per frame there; the console's is the number). The sdk-edits rules for `command_processor.cpp` are in
  their final form now (the 0.79 rule's anchor survives in its own `#else`, so corrective rules re-inserted blocks on
  every rerun: builds 0.201-0.203 failed with two `CpStats`); `chain5.sh` restores the file from git before a build.
  Not on the console: FTP off, the card not in the PC.
- **0.207-0.211 (`non_argument_as_local`, step 3; the spin off by default):** r0, r2, r11, r12, f0 and v32-v63 as locals:
  `ctx.r11` accesses 236 (from hundreds of thousands), `ctx.f0` 17. The SDK's generator localizes the non-argument
  set unconditionally; sdk-edits rule 23c makes it honour `share_registers` for that set too and hands every
  localized r/f/v over around calls to such functions (the stock hand-over covered r14-r31 only). Marked
  `share_registers` in the manifest: the seven hidden-argument readers of the audit (f0: `sub_8230B2D0`,
  `sub_8281BC00`, `sub_827F8280`, `sub_82848684`, none with a direct caller; r11: `sub_82796928`, `sub_82641A50`;
  r0: `sub_825C2C50`) and the stack probe `sub_8273ADC4` (r12 = the frame size at entry, `r11 = -r12`, three direct
  callers). `tools/locals_audit.py` after it: 0 GPR non-argument cases, 192 of the 226 vector cases inside the two
  vector save/restore helpers, 21 in two software-pipelined loops (`sub_827218A8`, `sub_826E8348`: sa2-nx's class of
  shift registers read before any write). Eden gate passed (stage, HUD, R3 zoom); the fps run coincided with
  hcu-switch's SDK build (title 23-24 fps instead of 35-44): re-measured in an idle window.
- The WAIT_REG_MEM spin is off by default since 0.207 (`cpspin_us` turns it on): the one scheduling change between
  0.187 and 0.200, suspected for the 17/33 ms alternation; the console A/B (0.200 + `cpspin_us` = 0) is pending.
- **0.212-0.213 (per-handle multi-waiters, from hcu-switch 7ea4bde):** `WaitMultiple` registers a small waiter (mutex,
  condvar, flag) on each handle under that handle's `mutex_` and sleeps on its own condvar; `NotifyMulti()` is a member
  called with `mutex_` held after the state change and wakes that handle's waiters only. 0.69-0.211 had one shared
  mutex + condvar + generation: every signal of every object took the mutex and woke every multi-waiter (the XMA
  decoder and the audio worker sit in WaitAny permanently). **Eden, clean window: stage 36.4 fps** (0.206: 32.8;
  0.184: 27.3; 0.177: 21.0), frames **33% under 17 ms, 49% at 33, 18% at 50** (0.200: 0-7 / 72 / 25), picture, HUD and
  enemies intact, 0 crashes. Candidate for the console: `/root/cvhd/stage_0.213` (0.213.06a49a4, NRO 81.8 MB).

### 2026-10-29 (night): Eden as the primary measure: busy ms per frame per thread, calibration against the console

- The user's scheme: Eden fps does not transfer (0.177: 21 vs 22; 0.187: 28 vs ~50; 0.200: 28.6 vs 40-45), the CPU
  work per frame does. **0.215:** `[nx] busy/frame (N frames): <thread> X ms (P%) | ... sum S ms` every 2 s from
  `switch_nx.cpp`: busy = wall minus the ticks the thread spent in the SDK's wait primitives (`WaitScope` in
  `threading_switch.cpp` adds them through `rex_nx_waited_add`, a thread-local slot, no lock; Eden's
  `ThreadTickCount` is 0, so this is the measure that works in both places). Sleeps (`svcSleepThread`: the rmutex
  yields, the 1 ms WAIT_REG_MEM polls) and the guest's own spins (`sub_82620BD0`) count as busy. The fps line
  carries `p50 p95 p99` of the second's frame times next to the histogram; the HUD shows "fps main-busy-ms".
  Calibration table: `D:\git\dev\console-shots\calibration-cvhd.md` (build, Eden fps, Eden busy, console fps,
  console histogram, date); push only a build whose busy numbers are clearly better.
- 0.213 is on the console (FTP came back; PUSH OK, SHA-1 2e77e8ce9a28b6b100d784e110f41d057c2450ac); no run yet.
- **0.218-0.219:** the busy line counts sleeps, the swapchain acquire and the fence wait as waiting; threads that never
  pass a primitive print "(no waits seen)" (two guest threads blocked in a condvar outside our scopes). **The
  command thread's non-draw work was the register writes:** SET_CONSTANT / LOAD_ALU_CONSTANT went through the
  virtual `WriteRegister` per dword (bounds check, register-info lookup, the scratch/COHER/LUT switch) ~80,000
  times a frame; `WriteRegistersFromMem` is overridden for the constant ranges (0x4000-0x4927: nothing special
  there) with a straight byte-swapped copy and one dirty mark. Eden busy/frame: GPU Commands 18-19 -> **14.0-14.5
  ms**, main 10.7-11.4, the four workers 6-8 each; stage 35.0 fps with 28% of the frames under 17 ms. The console
  pair for these builds is still missing (0.213 is there, not yet run).
- **0.221 (hook `src/guest_hooks.cpp`: the game's GPU-wait iteration `sub_82620BD0` sleeps 20 us before its 32x
  `db16cyc` spin; a strong `sub_82620BD0` replaces the weak alias of the generated code):** Eden stage 35.0 fps,
  27% of the frames under 17 ms, busy main 11.1-12.2 (the sleeps counted as busy), GPU Commands 13.5-14.1, workers
  5.5-7.4: no visible change against 0.219. On the console the spin held core 0 at 0x3A (the main thread never
  yields there); the hook makes it sleep instead. 0.222 counts the hook's sleep as waiting (busy = real work).
- The generated code is compiled at **-O2**: devkitPro's `dkp-rule-overrides.cmake` sets Release to `-g -O2`,
  the SDK gives itself -O3 (`CMakeLists.txt:142`). 0.223 replaces -O2 by -O3 for the game project (CMakeLists.txt,
  Switch block); compared by the busy ms per frame of the workers and main in Eden.

### 2026-10-29 (late): -O3 for the guest code (0.223), the XDK CRT copies as host calls (0.224)

- **The game's CRT, found by call-site count and first instructions** (`generated/default`, comments of the
  generated code): `sub_8273A030` = memcpy (1208 direct call sites + 7 tail jumps; `std r3,-8(r1); clrlwi
  r6,r3,29; dcbt r0,r4`, byte/word/dword loops, a 128-byte path), `sub_8273AD00` = memmove (`cmpw r3,r4;
  beqlr-; bge+ ...; b memcpy` for dst < src, else a backward loop), `sub_8273A590` = memset (606 sites; `addi
  r0,r5,1; mtctr r0; ori r6,r3,0`, the byte broadcast by `rlwimi`). `sub_827388E8` is memcpy_s (errno 22/34, calls
  memmove). Each character DLL carries its own copy at the same relative offsets: alucard 83024B90/830250C0/83024AF0,
  axearmor 83530840/83530D70/835307A0, charlotte 831D1A30/831D1F60/831D1990, jonathan 83142B20/83143050/83142A80,
  shanoa 83262AE0/83263010/83262A40, soma 830B2A90/830B2FC0/830B29F0 (memcpy/memmove/memset; the Soma DLL has 54
  memcpy, 12 memset, 11 memmove call sites). The function table (`cvhd_register.cpp`) references the `sub_` alias,
  so indirect calls take a hook too.
- **Hooks (`src/guest_hooks.cpp`, 0.224):** a strong `sub_X` replaces the weak alias of the generated code (as the
  0.221 GPU-wait hook); the body is one `std::memmove` / `std::memset` on `GuestPtr` host pointers (the Switch fold)
  plus `rex_nx_mark_range` for the GPU's dirty pages (the recompiled loop marked every store; a host copy marks
  nothing by itself: the Konami-logo stale-texture class). r3 is left as the return value. memcpy is memmove: the PPC
  loop copies forward and an overlapping `std::memcpy` is undefined. For the DLLs the aliases are localized by the
  module link (0.134 rule), so `tools/module_keep_symbols.py` reads `src/guest_hooks.cpp` and keeps every hooked
  name global; the strong definition then wins at the final link. Check in the ELF: `nm cvhd.elf | grep -E
  " [A-Za-z] sub_(8273A030|830B2A90)$"` shows one `T` each. The SDK's own route is `[rexcrt] memcpy = 0x...` in the
  manifest (codegen renames the function to `rexcrt_memcpy`, `src/kernel/crt/memory.cpp` provides it), but its
  `native_memcpy` marks no dirty pages and `HostToGuestFunction` turns the returned pointer into `host - base` (the
  A0 alias of a C0/E0 address): hooks of our own avoid both and need no codegen rerun.
- **0.223 in Eden (clean window, load 0.13): -O3 gives nothing Eden can see.** Stage 33.9 fps (0.221: 35.0), 22% of the
  frames under 17 ms (27%), busy/frame: workers 6.6-6.7 ms (6.1-6.3), GPU Commands 15.1 (13.9): within run-to-run noise
  or slightly worse. Eden runs the aarch64 code through its JIT, so it cannot rank -O3 (vectorised, unrolled code
  translates differently); the console pair decides. **Main's busy is 1.8 ms per frame** once the GPU-wait sleeps of
  the 0.221 hook count as waiting: the main thread's own work is small, it waits for the command processor (Eden:
  15 ms of recording per frame) and the workers. On the console the command processor's 8 unmet WAIT_REG_MEM polls per
  frame each sleep 1 ms before the main thread can continue: **0.227 adds the knob `sdmc:/switch/cvhd/cpsleep_us`**
  (the sleep per unmet poll, default 1000 = the SDK's 1 ms cap; 100 is the value to A/B on the console; `cpspin_us`
  stays the spin before the first sleep).
- **The 0.226 build carried no hook (rsync mtimes):** `rsync -a` keeps the Windows mtime, and the hook file edited at
  21:32 UTC was older than the object compiled at 21:33 by the previous build; ninja saw nothing to do and the ELF
  still had `W sub_8273A030` (the weak alias). The chain scripts (`/tmp/chain5.sh`, `chain6.sh`, `regen.sh`, copies in
  the scratchpad) rsync with `--no-times --checksum` now: a changed file gets the transfer time. The module keep list
  had the same hole: the join command depended on the module objects only, so a hook for a DLL function never reached
  the keep list; the template (sdk-edits, `dll_targets_cmake.inja`) adds `tools/module_keep_symbols.py` and
  `REXGLUE_MODULE_KEEP_DEPENDS` (the game's `src/guest_hooks.cpp`) to DEPENDS. Check after every hook change:
  `nm cvhd.elf | grep -E " [A-Za-z] sub_(8273A030|830B2A90)$"` = `T`, not `W`/`t`.
- **0.229 in Eden (clean, load 1.25): the CRT hooks bind in all seven modules** (`nm`: `T sub_8273A030`; the Soma code
  calls `bl 368a4e0 <sub_830B2A90>` = the hook; the recompiled bodies `__imp__sub_830B2A90` are gc'd). Two more holes
  on the way: `tools/module_keep_symbols.py`'s `main()` never passed the hooked names (the self-test covered
  `keep_list` only; fixed, the self-test now asserts the file is read), and the sdk-edits rule for the module template
  re-applied itself once its `new` text changed (the `old` line is inside the `new` block): the chain scripts restore
  `dll_targets_cmake.inja` from git before every sdk-edits run, like `command_processor.cpp`. Stage 35.5 fps, 29% of
  the frames under 17 ms, busy/frame main 1.6, GPU Commands 13.9, workers 6.1 x4 (0.223: 33.9 / 22% / 1.8 / 15.1 /
  6.7; 0.221: 35.0 / 27% / 13.9 / 6.2): back to the 0.221 level, within noise. Picture intact (stage overview, HUD).
  **Eden cannot rank -O3 or the CRT hooks**: both are within +-1 ms per thread there; the console pair decides.
  Candidate for the console: `/root/cvhd/stage_0.229` (SHA-1 f571bda756d264052db14d89cd6286168aeb0dd3, 84.4 MB; ELF
  `/root/cvhd/elf/cvhd-0.229.2aee311.elf`). Console plan: run 0.213 (on the SD) for the first calibration pair, then
  0.229 as it is, then 0.229 with `sdmc:/switch/cvhd/cpsleep_us` = `100`.
- **Eden profile of 0.231 (`prof`, the new `[nx] pctop` cumulative histogram, 4 KB buckets, 31,000 samples per
  thread):** the 2-s `[nx] pc` line's top 6 could never show guest code (two svc buckets take 80%, the rest is spread
  over thousands of 256-byte buckets at 0-2%); `switch_nx.cpp` keeps a cumulative histogram per sampled thread now and
  prints the top 24 every 30 s. Symbolize with one addr2line call for all offsets (`scratchpad/pctop.sh`; a process
  per address took minutes on the 660 MB ELF). Note: the symbol of a 4 KB bucket is the function at the page start,
  not necessarily the hot one; the pages next to `Deko3dCommandProcessor::IssueDrawImpl` are its own code
  (spdlog/fmt/`rex::log::gpu()` names there are template instantiations at page starts, not logging).
  Workers: 83-87% in the svc stubs (waiting for jobs; the prof run itself lowers the fps), then
  `RtlEnterCriticalSection_entry` 1.2% + `atomic_cas` 0.3-0.5% (the XDK critical sections: a CAS spin up to
  `spin_count`, the workers contend on the game's own locks; ~10% of their busy time) and guest functions at 0.1-0.4%
  each (`sub_825E34F0`, `sub_825EB778`, `sub_825D91E8`, `sub_8255F918`, `sub_825CE570`): **a flat profile, no single
  guest hot spot**. GPU Commands: 67% svc (the WAIT_REG_MEM sleeps and idle waits), then IssueDrawImpl and its
  pages ~25% of its samples; the phase split in Eden: uni 202-231 of 334-349 ms/s (60%: Eden's write tracking of the
  frame data ring, console 0.187 had uni 20 of 175), tex 45, vtx 33-45, sync 22, state 10.
  `[nx] gpu waits: 582 sleeping polls (582 ms asked, 802 ms really slept)` per 2 s = 9 polls per frame, 1.4 ms each
  in Eden: the main thread waits behind them (`sub_826275C0` waits for the ring), so the poll sleep is frame latency.
  A/B in Eden with `cpsleep_us` = 100 follows.
- **The poll sleep: 1 ms -> 100 us (0.233, the first Eden-visible gain since the bulk register writes).** A/B on 0.231
  by the file `cpsleep_us` = `100` (clean window): stage **37.9 fps, 39% of the frames under 17 ms, 9% at 50 ms** against
  35.5 / 29% / 17% with the 1 ms sleep; sleeping polls 582 -> 1200 per 2 s, really slept 802 -> 587 ms. The command
  processor answers the game's fences within ~0.1 ms (Eden: ~0.5 ms) instead of 1-1.4 ms, and the main thread's wait for
  the ring shortens with it. 100 us is the default since 0.233 (`switch_nx.cpp`; the file still overrides); the
  gate run of 0.233: 36.9 fps, 35% under 17 ms, 9% at 50 ms, picture intact, 0 crashes. **Console candidate:
  `/root/cvhd/stage_0.233` (0.233.7a1110c, SHA-1 79e25cc20e042ccf144e361617f2a71645255cb8).** On the console the gain
  should be larger (a 100 us sleep is 100 us there; 0.187 had 8 one-millisecond polls per frame = up to 8 ms of latency
  on a 20 ms frame). If it is jerky there, `cpsleep_us` = `1000` restores 0.187's timing without a build.


### 2026-10-30: multiplayer recon (no build change): the 360 game has no local co-op; Eden runs used the user's Eden copy

- **Verdict: Xbox LIVE only.** `game/ArcadeInfo.xml`: `MinPlayers="1" MaxPlayers="1"` (the XBLA offline player count; the
  title update's copy has no player fields). The main menu's "Multiplayer" item (Eden, `E:\conker-data\eden-runs\
  cvhd-0.233.7a1110c-deko3d-multiplayer3\shot-07-0.png`): "You must sign in to Xbox LIVE to choose this mode." (the
  SDK's user 0 is signed in locally: `user_profile.h` `signin_state() = 1`; LIVE would be 2, and
  `XamUserCheckPrivilege` answers 0 = denied for every privilege). The menu's six slots are LIVE session members
  (`XamSessionRefObjByHandle` 16 call sites, the XGI session app through `XMsgStartIORequest`, `XNetQos*`,
  `XNetRegisterKey`, `NetDll_WSASendTo/RecvFrom`: the game's own UDP netcode). The text resources
  (`data/text/textres_*.txrc`, `TXRC` + a per-string bit-packed table) are not plain UTF-16; not decoded.
- **Pads.** The game calls `XamInputGetState` through one wrapper (`sub_82749068`, user index = field +16 of a
  pad object polled by `sub_825F3A18`); the pad object constructor `sub_825C7228` is called with index 0 once at
  init (`cvhd_recomp.18.cpp`) and from the session member list with the member's controller index
  (`sub_825C6DD8`, a flag byte at +32 marks local vs remote members). So pad 0 is the only local pad on the 360
  too. Our side: `nx_input_driver.cpp` enumerates one device ("Switch pad", `padInitializeDefault` = handheld /
  No1 / Joy-Con pair), the SDK's `SlotAssignment` maps device ordinal N to guest user N (`kMaxGuestUsers` 4), users
  1-3 get `X_ERROR_DEVICE_NOT_CONNECTED`. Four pads would be ~40 lines (`padConfigureInput(4, ...)`,
  `padInitialize(&pad[i], HidNpadIdType_No1 + i)`, four `DeviceInfo`s with ordinals 0-3), but the game has no mode
  that reads them.
- **Local multiplayer would need the PS3-style mode the 360 binary does not have**: a local XSession stub (the XGI app
  already answers success to create/join/start) plus a fake second member with `local` flags and its own pad index,
  i.e. the game's session/member code driven without XNet. Unknown size; the netcode (UDP between consoles) would
  still think the second player is remote. Not started; needs a decision.
- **Eden copy (found on the way):** `switch/eden-shot.ps1` launched `E:\Switch\Eden\eden.exe` (the user's copy) by a
  hard-coded path; `eden-deko3d.ps1` staged the NRO into `E:\Switch\Eden-cvhd\user\sdmc` but the process used the
  user's config, so `sdmc:/switch/cvhd/` was the user's folder: its `pad.txt` (the perf key set, which is why the perf
  runs were right), its shaders, and our logs went there (today's runs `multiplayer` and `multiplayer2`;
  `E:\Switch\Eden\user\sdmc\switch\cvhd\cvhd_log.txt` 10:19 is ours). Last night's gate233 `eden_log.txt` shows
  `E:\Switch\Eden-cvhd\user\config` though, so which copy ran before today is not certain from the scripts alone.
  `eden-shot.ps1` has `-Eden <exe>` now and `eden-deko3d.ps1` passes `E:\Switch\Eden-cvhd\eden.exe`; the run's
  `eden_log.txt` (copied next to the screenshots) names the config dir: check it when a run looks odd.

### 2026-10-30: the fps mode file (60 / 30 / 30skip, 0.236-0.237): the game is time-based, "30" is the even one

- Request (the user, through the parent session): the console runs 0.233 at ~40 fps, uneven; an even 30 would be better.
  hcu-switch's game is frame-locked (a plain 30 cap halves its speed), so it has "30skip" (the guest keeps 60 frames,
  every second one is drawn; hcu commits 86f0377, 6a235fd, 839cd34).
- **`sdmc:/switch/cvhd/fps`** (`src/gpu/deko3d_backend.cpp` `ReadFpsMode`, read at start and every 60 swaps, so it
  switches during a run, no rebuild): no file or `60` = every frame, presented at the next vblank (as before); `30` =
  every frame drawn, presented every second vblank (`dkSwapchainSetSwapInterval(2)`: the swapchain acquire paces the
  guest at 30 Hz); `30skip` = the odd swaps are not drawn (`Skipping()`), the guest runs free. The `dk time/s` line
  carries the **shown-frame intervals** (`shown N: interval p50 X p95 Y ms, Z% <= 34 ms`, from the presents); the
  `[nx] fps` line counts guest swaps (in 30skip: ticks, not pictures). The `[nx] gpu: swap #N` line prints in 30skip too.
- **Is the game time-based? Yes, with a clamp.** The stage's HUD countdown against the wall clock over 90 s (screenshots
  30 s apart, Eden-cvhd, WSL load 0, the user's Eden running Kefir Hub in parallel):

  | mode | guest swaps/s | frames <=17 / <=34 / <=51 ms | shown/s | shown interval p50 / p95 | <= 34 ms | game time per wall time |
  |---|---|---|---|---|---|---|
  | 60 | 35.7 | 31% / 59% / 9% | 36.3 | 33.0 / 34.5 ms | 90% | 88.96 s / 90.85 s = **97.9%** |
  | 30 | 30.0 | 1% / 80% / 17% | 30.5 | 33.5 / 33.5 ms | **98%** | 87.30 s / 90.87 s = **96.1%** |
  | 30skip | 56.8 | 86% / 12% / 0% | 28.9 | 33.5 / 50.0 ms | 77% | 90.35 s / 90.87 s = **99.4%** |

  (stage = the last 40% of the fps lines, 128 s; logs and screenshots in `E:\conker-data\eden-runs\cvhd-0.237.7144681-
  deko3d-fps{60,30,30skip}\`; the gate233 run of 0.233 at 36 fps gave 59 s / 60.5 s = 97.5%.) A frame-locked game would
  show 50% at the 30 cap; the deficit instead follows the share of 50 ms frames (17% in the 30 run: Eden's own
  slowness in the early stage; 9% at 60; 0% in 30skip): the game clamps its time step on a long frame. At a steady 30
  (every frame under 33 ms, which the console does: 0.233 runs ~40 fps there) the speed is 100%.
- **Verdict: `30` is the even mode** (98% of the shown intervals at 33.5 ms, p95 = p50 in Eden). `30skip` shows fewer
  pictures (28.9/s) at uneven intervals (p95 50 ms: the drawn frame after a skipped one lands wherever the guest's tick
  pairs fall; hcu's busy-GPU skip, 839cd34, would be the next step if the console wants it); its only merit is the
  guest tick rate (57/s in Eden). Eden honours the swap interval (30.0 fps, p50 33.5 ms from the first line).
- Staged for the console, not pushed: `/root/cvhd/stage_0.237` (0.237.7144681, NRO SHA-1
  95d0d4492e5e10166ea16c85ff4ccc77d3275fa5, 26 shaders, the `deko3d` marker). On the console: write `30` into
  `sdmc:/switch/cvhd/fps` (or `60` / `30skip`; a missing file = 60) and read the `[nx] fps` line (p50/p95, the 17/34/51
  histogram) and the `shown` part of the `dk time/s` line; the file can change while the game runs.
- Not measured: movement and music (sound is off in Eden); the "100% at a steady 30" is inferred from the clamp
  reading, Eden never gave a window with zero 50 ms frames. The user checks the HUD countdown against a watch on the
  console. 30skip ceiling to name: a resolve or upload that happens once could land on a skipped frame and never fill
  its image (the 30skip screenshots look intact).
- Eden runs: the idle wait before a run should block only on an `Eden-cvhd` process, not on every `eden.exe` (the
  user's `E:\Switch\Eden` ran Kefir Hub for another session during these three runs). A detached
  `Start-Process bash` series never got past its first `wsl` call (empty log, processes alive): run such series
  through the Monitor tool instead. HEAD ec680e0 differs from the staged 0.237 (7144681) by comments and this file
  only: the same code.

### 2026-10-30: thread priorities (0.238-0.244): the SDK ignored the game's, the job workers now run above the background threads

- Task (the parent session): cheap speed wins toward 60 fps while the user tries 0.237 on the console (~40 fps at
  0.233). Facts first: no console log exists for 0.233 (the user's run); the latest console profile is 0.200
  (`[nx] cpu` ~250% of three cores: GPU Commands 35%, the four workers 28-34% each, main 13-15%, the rest ~60%).
- **The SDK ignores every thread priority and affinity the game sets**: `XThread::SetPriority` maps the
  KeSetBasePriorityThread increment to five levels and then checks the cvar `ignore_thread_priorities` (default
  **true**); `SetActiveCpu` (XSetThreadProcessor) is a no-op on the Switch (sdk-edits block 17). And even with the
  cvar off, the posix shim `pthread_setschedparam` (`switch_compat.c`) acts on the calling thread only.
- **What the game asks (Eden 0.241/0.242, `[nx] game priority` / `[nx] game cpu` lines):** its first four threads
  (handles F8000040/44/48/50 in every run; 7.0-7.5 ms of work per frame each = the job workers main waits for) ask
  NORMAL (increment 0) and hardware threads 0-3; the audio mixer (3.2-3.6 ms per frame) asks TIME_CRITICAL (16); a
  streaming thread toggles 1 -> 16 -> 0 seven times a second; one helper asks HIGHEST (2); the other nine ask NORMAL.
  So honouring the game's priorities alone puts the background threads **above** the workers: the opposite of what
  the console profile wants (the workers are the critical path, main waits 80% of its time for them).
- **0.244-0.247 (`switch_nx.cpp`):** (1) the game's first four `XThread*` names get base priority **0x3A** (cooperative,
  main's level) with the file `sdmc:/switch/cvhd/boost`: a worker that wakes for a job preempts the time-sliced 0x3B
  background threads on its core instead of queueing behind their 10 ms slices. (2) The game's own requests reach
  Horizon with the file `gameprio` (`rex_nx_guest_priority`: above normal -> 0x3A, time critical -> 0x39, never
  below a thread's base; the mixer then sits at 0x39 above the workers). (3) `rex_nx_guest_cpu` logs the game's
  hardware thread per thread (not applied: the game spreads workers and helpers alike over all six). **Both knobs
  are off by default: the staged build schedules as 0.237 did** and only logs (`[nx] job worker ...` x4 with `boost`,
  `[nx] game priority ...` capped at 8 lines per thread, `[nx] game cpu ...`). Pitfall: the SDK names a guest thread
  twice (without and with its handle), so the worker count counts threads, not names (0.243 boosted two of four).
- **Eden could not rank the scheduling (same day, same window, each run 330 s, load < 1):**

  | build | scheduling | stage fps | frames <= 17 ms | workers busy ms/frame |
  |---|---|---|---|---|
  | 0.237 (afternoon) | as before | 35.7 | 31% | 6.3-6.4 |
  | 0.241 | game priorities honoured (mixer 0x39) | 32.7 | 16% | 7.3-7.5 |
  | 0.244 | + four workers 0x3A | 31.5 | 14% | 7.2-7.4 |
  | 0.246 | workers 0x3A only | 29.4 | 2% | 7.5-8.6 (series 6.5-7.5) |
  | 0.247 (gate, load 2.2 at start) | as before (both knobs off) | 32.6 | 14% | 6.8-7.2 |

  Every change lost fps against the afternoon run, and so did each later run against the previous one (a drift of the
  evening's environment as much as the change: the workers' busy series overlap 0.237's). Picture intact and 0
  crashes in all of them. The console decides: 0.247 as it is, then with the file `boost`, then `boost` + `gameprio`;
  read `[nx] fps` (the 17/34/51 histogram), `[nx] cpu` (the workers' share, main's wait) and listen for audio
  drop-outs with `gameprio` (the mixer above the workers).
- Staged, not pushed: `/root/cvhd/stage_0.247` (0.247.ba3af7b, NRO SHA-1 db7d0c835bc0a15f00d37568040bb01e2c7a945c,
  84,363,461 bytes, 26 shaders, the `deko3d` marker; the fps file of 0.237 works as before).
- **`skip_lr` audited, not enabled** (the one codegen flag left): 89,508 `ctx.lr = const` stores (one per call),
  13,822 mflr, 6,425 mtlr, 0 PIC `bl`+`mflr` pairs, but 14 `mtlr r3; blrl` sites (`_CallSettingFrame`, the SEH
  funclet dispatcher, one per module x2) break under it (mtlr skipped -> stale LR), and the skipped mflr leaves
  garbage in the saved-LR stack slots the SEH unwinder walks. Gain class 1-2% (a store per call); needs an SDK edit
  (keep mtlr) to be safe. Parked.
- **What is left that is cheap: nothing with a number.** Beyond this (unmeasured on the console) and `skip_lr`
  (1-2%), the levers need a console `prof` run (the `pctop` histogram of the guest code: a hookable hot function) or
  expensive work (LTO/PGO, more native hooks, a per-frame job graph). The chain script restores
  `src/system/xthread.cpp` from git before sdk-edits now (block 17's text changed).

### 2026-10-31: NRO icon, Switch button glyphs, the in-game settings menu, rumble (0.248-0.25x)

- **NRO icon from the game package** (the parent session's request): the STFS header's title thumbnail (offset
  0x571A, the same PNG as the package thumbnail at 0x171A and as `x_ttimg0.png` in the package: 64x64, md5
  725d2c83935f1a6983e7cb0f5084d626) upscaled to 256x256 JPEG (md5 0e003a7e406bacb435aecf56889b52dc) and written
  into a copy of the 0.247 NRO's asset section (`nro_set_icon.py`: the ASET header is rebuilt, the NACP and RomFS
  kept; the NRO body is byte-identical). Nothing of it is in the repo: the icon and the script live in
  `D:\Downloads\x360\Castlevania Harmony of Despair\cvhd-icon\` and `/root/cvhd/icon/` (`stage_0.247i`).
- **Where the on-screen button prompts come from** (five candidate sheets swapped first with no visible change,
  a frame trace of the menu found it): the main menu's and the pause menu's "A OK / B Back" circles are two 48 px
  quads from **`data/menu/ranktex_0.dds`** (DXT3 256x384: a row of coloured circles A B X Y at x 4-42 / 52-90 /
  100-138 / 148-186, y 33-72), not the `convsym` font (a blank convsym page changed nothing) and not the 4bpp
  sprite pages. The 4bpp pages (`data/opd/**/*.csr`: 128x128, header 0x30, 64 bytes per row, 8 zero rows of
  padding at the end; palette `c_*.col` 4096 entries; `p_*.opd` sprite tables with readable names) hold the small
  white glyph sets: `f_ui_btn0` (A B X Y G, LT RT LB RB, arrows), `ui_obj01` (41 px circles, the A cut by the page
  edge into two copies), `f_ui_equip06/07` (X Y / A B cells), `f_ui_equip10-13` (whole pages A/B/X/Y + arrows),
  `f_ui_select00` (a column of circles). `arc/font.arc` (zlib entries, table of (name, offset, usize, csize), data
  aligned to 0x40) holds the fonts: `convsym.0.dds` 1024x512 DXT1 (linear, 16-bit words byte-swapped), glyph cells
  52x28 in kbf record order (record 33 = 'A', 34 = 'B', 56 = 'X', 57 = 'Y').
- **The swap is done in memory at file read time** (`switch/host/nx_glyphs.cpp`, called from the Switch bounce
  copy of `XFile::Read`, sdk-edits rule in `xfile.cpp`): `ranktex_0.dds` A<->B and X<->Y as 12x11-block DXT3 cells,
  the 4bpp pages by byte-aligned column cells or row ranges; the game data on the card stays original and nothing
  of the game is in the repo. The setting BUTTON ICONS (XBOX / SWITCH, default SWITCH) turns it off at the next
  start. Verified in Eden: the menu shows "B OK / A Exit Game" (the Switch's B is the bottom button = 360 A).
  Not swapped: ui_obj01's A/B (the two cut copies), `f_ui_equip10-13` (whole-page swaps need another file's
  content), the Help & Options controller picture (`menu/controller*.dds`, an Xbox pad drawing). The offline
  tools of the hunt (`swap_glyphs.py`, `font_swap.py`, `ranktex_swap.py`) are in the session scratchpad only.
- Eden pad.txt seconds count from the pad driver's first poll (~8 s after Eden starts); START at 40/55/70 s
  reaches the main menu at ~100 s driver time in every run (one of those lands on the title).
- **Settings menu** (`switch/host/nx_menu.cpp`, model: Conker's `clx_menu.c`): minus opens/closes it (the 360's
  BACK = the LIVE quick-chat wheel, useless offline), up/down pick, left/right or A set, B/START/minus close and
  save `sdmc:/switch/cvhd/settings.txt` (`fps=0|1|2 rumble volume fps_counter button_icons`; the marker files
  `fps`, `mute`, `norumble` are imported once when there is no settings file). While it is open the game sees an
  idle pad and keeps running. Drawn by the present pass: `present.frag` is a text grid (cols x rows cells of 6x8
  units, 5x7 glyphs as two integer-valued floats per cell decoded with floor/mod; one int uniform index; the dark
  box and the highlighted row from a per-row colour), the fps counter is the same grid with one row. Eden:
  the menu draws and takes input (RUMBLE -> OFF, VOLUME -> 90 saved), Eden's shader recompiler survives it.
- **Rumble** (`nx_input_driver.cpp` `SetDeviceVibration`): the 360's left/right motors -> `HidVibrationValue`
  amp_low/amp_high (160/320 Hz) on both Joy-Cons / the Pro Controller (No1 handles for the style at start) or the
  handheld set; the game calls XamInputSetState ~25 times a second (zeros when idle). Not felt on hardware yet.
- **Pad polls**: `[nx] pad: N polls/s`: the game polls once per guest frame (27-28/s in Eden at 28 fps; 60/s on the
  console at 60 fps); the driver reads libnx's current HID state at each poll, no queue of its own.
- `dk_trace` takes a third word: `any N [M]` traces the first frame after N seconds regardless of its draw count
  (menus have 61 draws; the default gate is 440).

### 2026-10-31 (evening): console 0.249 in mode 30: even presents, uneven game steps; vblank at 30 Hz (0.250)

- **Console 0.249, the user's run** (`E:\conker-data\hub-logs\cvhd-0.249-console\`, 62 s in the stage, file `fps` = 30, the
  settings imported from it): `shown 30: interval p50 33.3 p95 33.4 ms, 96-100% <= 34 ms` (the presents ARE even), but
  the guest's swap-to-swap times per second: 0-5 frames of 17 ms, 16-25 of 33, 5-8 of 50 (the same 17/50 pattern Eden
  showed in the 0.237 mode-30 run). GPU 12-13 ms per frame (`gpu 363-403 ms/30 frames`); busy per frame: GPU Commands
  7-9 ms, main 1.6, the four workers 8-9 each, sum ~50 ms over three cores; `[nx] cpu`: workers 21-24% each, GPU
  Commands 20-21%. WAIT_REG_MEM: 3570 sleeping polls/s x 100 us (the pacing). The user's "uneven": the game times its
  frames by the time base (`mftb` in `sub_82748590` / `sub_82748800`; no KeQueryPerformanceCounter import) and starts
  them at the vblank interrupt (60 Hz from the SDK's vsync worker), while the present is paced at 30 Hz by the
  2-image swapchain acquire: the guest's frames come in pairs (17 then 50 ms), its time step jumps, the motion judders
  on an even display. **60 fps is out of reach**: ~50 ms of CPU per frame on three cores = 16.7 ms per core at 60 fps,
  no headroom; mode 60 gives 0.233's uneven 40-45.
- **0.250: in mode 30 the guest's vblank interrupt runs at 30 Hz** (`sdk-edits.py` rule 31: `rex_nx_vblank_divider` in
  `graphics_system.cpp`'s vsync worker; `ReadFpsMode` sets it with the swap interval; the file
  `sdmc:/switch/cvhd/vblank60` keeps 60 Hz vblanks = the 0.249 behaviour, for an A/B without a build). The log line:
  `[nx] gpu: fps mode 30: swap interval 2, ..., vblank interrupts at 30 Hz`. What decides on the console: the `[nx] fps`
  histogram over a minute (expected: nearly all frames <= 34 ms, no 17/50 pairs; a periodic 66 ms frame would mean
  the two 30 Hz clocks drift against each other: then fire the vblank from the present instead of the timer), and the
  game's speed (the HUD countdown against a watch: if the game counted vblanks for time it would run at half speed;
  then `vblank60`).
- **Green halos**: older than the glyph/menu/rumble work. The console shot of 0.141 from 2026-10-28
  (`D:\git\dev\console-shots\2026-10-28\cvhd\2026102815551500-*.jpg`, the stage overview) already shows the small
  sprites in green boxes, and the user's own Eden copy still holds the 0.112 NRO. The Eden 0.181 zoom-out shot shows the
  pipe room's vines dark olive where the console 0.249 shows bright green zig-zags with yellow fixtures: a palette-bank
  / palette-content class in PS B3EC630B (c[3]/c[254]/c[255]) rather than a format one (1x view intact). Two Eden runs of
  0.249 with a zoom-out were closed from outside (55 s and 134 s, a clean Eden exit with the config written, the
  window shrunk on the 105 s shot): no 0.249 zoom-out picture. The `dk_trace` file with the content `mip` next to
  the NRO traces the first zoom-out frame and dumps its images (`dump/NN.ppm`, the vines' k_16 index texture and the
  4096x1 palette): the next user zoom-out on the console gives the data over FTP.
- **0.250 in Eden, mode 30 (settings.txt `fps=1`), WSL load 4 (another session's `clx_recomp` at 260%):** the
  `[nx] gpu: fps mode 30 ... vblank interrupts at 30 Hz` line, title and stage: **0 frames of 17 ms** (0.249 mode 30 on
  the console: 0-5 per second), 19-25 of ~28 per second at 33 ms, 3-7 at 34-50 ms, 1-4 at 67 ms: a frame that misses
  its 33 ms slot now costs a whole slot (67 ms) instead of 50; in Eden under load the stage frames are 25-35 ms so
  misses are frequent, on the console (15-20 ms frames) they should be rare. The `shown` intervals: p50 33.5, 67-83%
  <= 34 ms (0.249 console: 96-100%). Game speed (HUD timer vs wall clock) NOT measured: the screenshots of the run
  are partial (below) and the run with two front-buffer dumps 60 s apart was closed from outside at 62 s (the fourth
  Eden run closed today). The user checks the speed on the console; `vblank60` is the way back.
- **Eden shows the game in a top-left part of its window (the user's screenshot, 2026-10-31 evening): not a build
  regression.** Measured over the saved screenshots (the lit region of the client area): 0.247 `default247` full
  (1616x899 of 1918x899) in every shot; 0.247 `glyphs4` partial in the first two shots (500x81, 116x25) and full from
  the third; 0.248/0.249 gate runs 167x34 throughout; 0.250 `mode30` 1055x383. The same build shows both, and the
  region changes with Eden's window size (1934 / 1857 / 2309 px wide between runs: the window was resized on the PC):
  Eden's placement of the 1280x720 layer, not the present pass (one full-screen triangle, viewport 1280x720 on
  1280x720 swapchain images, unchanged since 0.141). The console draws full screen in every user screenshot.
