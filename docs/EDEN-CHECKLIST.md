# Eden checklist for the local session

Run each item with `switch/eden-deko3d.ps1` (screenshots every 5 s) and relay the screenshots and logs.
Before the first run: pull `origin/main`, run codegen again (the manifest has six DLL modules now), and build with
`switch/build.sh`.

## 0. Rebuild steps that changed

1. `cd /root/cvhd/src && rexglue codegen cvhd_manifest.toml --ignore-stamp` with the **host CLI the SDK build
   made** after `python3 switch/sdk-edits.py /root/cvhd/rexglue-sdk` ran (the templates changed:
   `REX_TABLE_BASE`, module registry, DLL targets). It writes `generated/dll_*` next to `generated/default`.
   Never copy the `generated/` tree of the cloud box: it is game-derived and not in git.
2. Rebuild the Linux binary once so the codegen stamp and the host CLI match (`scripts/linux-build.sh`), then
   `bash switch/build.sh` (the Switch build links the six DLL modules as objects: each module is joined with
   `ld -r`, every global symbol but its two entry points is made local with `objcopy`). If the link fails with
   "multiple definition", that step did not run: check `build/switch/rexglue_modules/*.o`.
3. Shaders: the stage needs more shaders than the menu. On WSL run `scripts/collect-shaders.sh` (Linux trace
   backend, about 3 minutes; it presses the keys for you) so that `/root/cvhd/shaders_trace` holds them all,
   then `switch/build.sh` turns them into DKSH. Without them the stage draws nothing. Shaders still missing in an
   Eden run land in `sdmc:/switch/cvhd/shaders/missing/` (the script copies them back).

## 1. Menu faults, fixed on Linux (check the picture)

| check | what to look for |
|---|---|
| title -> START -> menu | no colour noise at the left, right and bottom edges, no green dots |
| book pages | text complete ("Purchase", "Single", "Back": no missing h, i, k) |
| Single Player page | striped popup gone (the popup is "Select a Chapter" / "Change Characters" panels: parchment with text) |
| left column | six identical card backs is correct (empty player slots); the first card shows "User" and the character |
| page turn | the turning page shows mirrored text on its back; that is the real look |

If the noise is still there: add the line `--deko3d_poison=false`?  No such switch exists; instead send the
`[dk]` lines about "target" and "resolve" from `cvhd_log.txt` and the `dk_dump` readback (create the file
`sdmc:/switch/cvhd/dk_dump`).

## 2. Gameplay (the new part)

Keys (`-Keys`): `38:START`, `100:A`, then the Single Player page: `130:DOWN`, `134:DOWN`, `138:DOWN`, `142:DOWN`,
`146:A` ("Start the Game"; hold 400 ms or longer at Eden's speed).

Expected in the log: the guest loads `game:\data\player_dll\dllSoma.dll` (the default character), then runs.
Look for these lines in `cvhd_rex_log.txt`: `Function table initialized for module: code=83...` (the DLL) and no
`function not in function table`. If the last line appears, the static module registry did not take the DLL:
send the lines around it and `Failed to load shared library for module`.

Then the stage: the SDK log should show no new errors. Screenshot every 5 s for 60 s after the load. Report:
frames show a stage (background, floor) or not; the player sprite or not; a missing shader list; fps.

Controls: pad.txt line `<s>:RIGHT:2000` should move the player right; `<s>:A` jumps (check the key map of the game).
