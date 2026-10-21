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

