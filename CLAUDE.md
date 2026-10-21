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

(none yet)
