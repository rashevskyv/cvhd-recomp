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
1. `eden.exe -g E:\Switch\Eden\user\sdmc\switch\cvhd\cvhd.nro` (PowerShell `Start-Process`), watch
   `cvhd_log.txt` grow for a few minutes, close with `CloseMainWindow()`, force only after 15 s.
2. Pass = the log reaches the guest's frames (`[nx] gpu: swap #N` from the null backend, N growing) and
   the exit is clean; keep the logs in `E:\conker-data\eden-runs\cvhd-<version>\`.
3. Only then `switch/deploy.sh 192.168.50.69` (SHA-1 verified), and the console run checks the Vulkan path.
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
