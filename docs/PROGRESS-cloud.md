# Cloud session progress (Castlevania HD, deko3d backend)

Plain notes. Each item says what is fixed, what Linux verified, and what needs an Eden check.
The Eden checks are listed in `docs/EDEN-CHECKLIST.md`.

## The test host: the deko3d backend runs on Linux

- `src/gpu/dk_gl/` holds an OpenGL stand-in for libdeko3d. It uses headless EGL and Mesa llvmpipe.
- `src/gpu/deko3d_backend.cpp` compiles unchanged on Linux against it. The same register decoding,
  texture upload, resolve aliasing and shader loading run there.
- Shaders on Linux are the translator's GLSL, not DKSH. The shim loads `shader_<hash>.vert|frag|geom.glsl`.
- The shim keeps deko3d semantics: NDC y +1 is image row 0, packed 16-bit formats keep deko3d's channel order.
- Frames are written as PPM files: `CVHD_DK_GL_DUMP=<dir>:<every N swaps>`.
  Every uploaded texture is written too: `CVHD_DK_DUMP_DIR=<dir>`.
- Speed is about 1.5 swaps per second on four cores. The trace backend runs at 60 per second.
- Drive it with `scripts/linux-drive.py` (keys by log events). Build with `scripts/linux-build.sh`.
- Build notes for a fresh Linux box are in `docs/LINUX-TEST-HOST.md`.

## Fixed, verified on Linux

| fault seen in Eden | cause | state |
|---|---|---|
| striped popup, garbage in textures | `TextureInfo::pitch` is in texels. Untile wants blocks. DXT textures were read with four times the row pitch. | fixed: the backend uses `extent.block_pitch_h`. Linux dumps of DXT1/DXT3 atlases decode correctly. |
| thin letters dropped in text | filters came from the fetch constant only. The shader's tfetch instruction can override them. | fixed: `SamplerIndex` follows the SDK's Vulkan backend. The menu text is complete on Linux. |
| mirrored "Loading." | only repeat and clamp were mapped | fixed: all eight Xenos clamp modes map as in the Vulkan backend. Needs an Eden look. |
| colour noise at the edges | new render targets and resolve images held old pool memory | fixed: they start cleared. Needs an Eden look. |
| red/blue swapped 16-bit textures | deko3d RGB565/RGB5A1 put R in the low bits. Xenos puts it in the high bits. | fixed: BGR565/BGR5A1, 4-4-4-4 reversed. No such texture seen yet at the menu. |
| one translated shader missing at the popup | PS 4AF443BC48F62D38 had no DKSH | translates now (glslang accepts it). Needs the DKSH build. |

## Left column shows the same card six times

This is probably not a bug. The left column shows the player slots. Empty slots show the card back.
The texture atlas (DXT3 336x832) holds that card back at its top left. Linux shows the same six cards.

## Gameplay blocker found and worked on: the character DLLs

- After "Start the Game" the game loads `player_dll/dll*.dll` (XEX modules) and calls into them.
- The SDK supports several modules. The manifest now lists the six DLLs as `[[modules]]`.
- On Linux each is a shared library. On the Switch (no dlopen) they are static libraries.
  `switch/sdk-edits.py` renames their entry points and registers them through the kernel state.
- Image bases do not overlap, so the address-named functions stay unique.
- Status: see the end of this file.
