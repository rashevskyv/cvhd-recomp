# Castlevania: Harmony of Despair (Xbox 360) on Nintendo Switch

Static recompilation of the Xbox Live Arcade game for Nintendo Switch (homebrew NRO, libnx + deko3d), with Linux and
Windows builds as well (the SDK's Vulkan renderer), built on the [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk).
**No game data is included.** You need your own copy of the XBLA package (`tools/stfs_extract.py` unpacks it).

> **EXPERIMENTAL. READ THIS FIRST.**
>
> - **Performance is limited to about 40 fps.** In the 60 fps mode the game runs at 40 to 45 fps and the frame
>   pacing is uneven. The default mode locks the game to 30 fps. The CPU budget is used up: 60 fps is not
>   reachable with this approach on the Switch.
> - **No multiplayer.** The Xbox 360 game has online play through Xbox LIVE only and no local co-op. The port
>   cannot do either. Only one controller is used.
> - **Very little testing.** Only a few hours of single-player were played, on one console. Most stages, all
>   six characters and most menus have not been checked. All DLC load (see DLC), with very little testing.
> - **Graphical glitches are possible**, and so are **crashes and freezes**. Save data may be lost.
> - Use at your own risk. Quit with EXIT GAME in the port menu, not by resetting the console.

## Install

Download your platform from [Releases](https://github.com/rashevskyv/cvhd-recomp/releases). The release files contain
the recompiled game program, which is a derived work of the game, but no game data (no graphics, sound or text). Use
them only with a copy of the game you own. You can also build everything yourself (see Build).

First extract your XBLA package and its title update (TU, version 0.0.2.4, needed by DLC chapters 8-11):
`python tools/stfs_extract.py <package> <out>`. From the game you need `default.xex` and `data/`; from the title
update `default.xexp`, `data/player_dll/*.dllp` and its `data/` files.

**Builds from the repository after 0.300 are recompiled from the title update** and need its files; the release
0.292/0.295 is recompiled from the base game and must NOT get `default.xexp` (a patch the program was not made from
breaks it).

**Switch** (Atmosphère and hbmenu):
1. Unzip the Switch zip to the root of the SD card. It creates `sdmc:/switch/cvhd/` (`cvhd.nro`, `deko3d`, `shaders/`).
2. Copy `default.xex` and `data/` into `sdmc:/switch/cvhd/game/`.
   Builds after 0.300: also `default.xexp` into `game/`, the six `dll*.dllp` into `game/data/player_dll/`, and the
   whole extracted title update into `sdmc:/switch/cvhd/update/` (the game reads `update:\data\...`; the log
   prints a WARNING when these files are missing).
3. Start as a title takeover: hold R on any game and launch "Castlevania HD". Applet mode is not tested.

Logs: `sdmc:/switch/cvhd/cvhd_log.txt` and `cvhd_rex_log.txt`.

**Windows 10/11 x64 and Linux x86-64** (a GPU with Vulkan 1.1):
1. Unpack the zip (Windows) or the tar.gz (Linux).
2. Put `default.xex` and `data/` into a folder `game/` next to `cvhd.exe` (Linux: next to `cvhd`). Builds after
   0.300: the title update as on the Switch, `update/` next to the exe.
3. Start `cvhd.exe` (Linux: `./cvhd.sh`). Or pass `--game_data_root=<folder>`.

The PC builds were only checked up to the title screen. They do not have the Switch settings menu.

## DLC and the title update

All 12 DLC packages work in builds after 0.300: the six characters (**Simon, Richter, Maria, Julius, Yoko, Getsu
Fuma**), chapters 7-11 and the two BGM packs. Chapters 8-11 need the **title update** (TU, version 0.0.2.4), and
builds after 0.300 need it for every game, DLC or not. The release 0.292/0.295 is made from the base game: it must
not get the TU, and there chapters 8-11 crash the game about 40 s after start. Playing DLC content has had very
little testing.

The port includes no DLC and no title update. You need your own files: the STFS packages from an Xbox 360 hard drive
or USB stick. The DLC packages are in `Content/0000000000000000/58410A7A/00000002/` (one file per DLC, content type
2). The title update is a file named `TU_...` (content type `B0000`, title `58410A7A`), for example from an Xbox 360
system cache. Install them in one of two ways.

### Way 1: on the console (no PC tools)

1. Copy the package files as they are, without renaming or unpacking them, into `sdmc:/switch/cvhd/install/`
   (create the folder). Put the title update there too, if `sdmc:/switch/cvhd/update/` does not exist yet.
2. Start the game. Before the game loads, the port unpacks every package from that folder:
   - a DLC to `sdmc:/switch/cvhd/user/0000000000000000/58410A7A/00000002/<name>/`, with a licence header;
   - the title update to `sdmc:/switch/cvhd/update/`, and its `default.xexp` and `*.dllp` to `sdmc:/switch/cvhd/game/`.
3. Each package that installed moves to `sdmc:/switch/cvhd/install/done/`. You can delete those files afterwards.
   A file that is not a Castlevania HD DLC or title update stays where it was. The log `sdmc:/switch/cvhd/cvhd_log.txt`
   has an `[install]` line for every package.

The first start after you add packages takes longer (a 30 MB chapter pack takes a few seconds).

The same works on PC: an `install` folder next to the `game` folder.

### Way 2: with the script on a PC (Python 3)

1. Insert the SD card into the PC. Run, with all your packages (DLC and the title update):
   ```
   python tools/dlc_install.py <SD>/switch/cvhd "<path>/TU_1C422JQ_..." "<path>/Ch07..." "<path>/Add Character - Simon Belmont"
   ```
   `<SD>` is the root of the SD card. You can also use any folder and copy it to `sdmc:/switch/cvhd/` later.
2. The script checks every file: title `58410A7A`, content type 2 (DLC) or `B0000` (title update). It unpacks a DLC
   to `user/0000000000000000/58410A7A/00000002/<name>/` with a header in `.../Headers/00000002/`. It unpacks the
   title update to `update/`, and its `default.xexp` and `*.dllp` to `game/`.
3. Start the game.

On PC the user folder is different: `Documents\cvhd\` on Windows and `~/.local/share/cvhd/` on Linux. Use way 1
there, or run the script on any folder and copy its `user/0000000000000000` folder into the user folder, and its
`update/` and `game/` files next to the executable.

To remove a DLC, delete its folder and its `.header` file.

## Controls

Buttons are positional, as on the Xbox pad: Switch B = Xbox A, A = B, Y = X, X = Y. `+` = START, `-` = BACK
(the game's own menu). **L3 + R3** (press both sticks) opens the port's settings menu: frame rate (60 / 30 / 30 skip),
rumble, volume, fps counter, button icons (Switch / Xbox), exit game. Settings are saved in
`sdmc:/switch/cvhd/settings.txt`. The default is 30 fps with the fps counter on.

On PC a gamepad works as on the Xbox. At the first start the keyboard also plays as pad 1 (the SDK's keyboard
bindings: Enter = START, Space = A, Backspace = B, WASD = left stick); `cvhd.toml` next to the exe keeps the settings.

## Build

Switch: `switch/build.sh` in WSL (devkitA64 and libnx; the SDK patches are in `switch/sdk-edits.py`). Linux:
`cmake --preset linux-amd64-release` (clang 18+). Windows: clang with the MSVC target and the VS 2022 Build Tools
(the method of the Guardian Heroes port). You must supply the game files yourself: the recompiled code is generated
from your `default.xex` and is never stored in this repository. Details: [CLAUDE.md](CLAUDE.md) (dated findings) and
[docs/PORTING-EXPERIENCE.md](docs/PORTING-EXPERIENCE.md) (the whole porting method, in English).

## More

- Release notes (Ukrainian): [docs/RELEASE-NOTES.md](docs/RELEASE-NOTES.md)
- Porting guide for the next Xbox 360 title: [docs/PORTING-EXPERIENCE.md](docs/PORTING-EXPERIENCE.md)
- Castlevania and all game content belong to Konami. This is an unofficial fan project. It is not affiliated with
  Konami, Microsoft or Nintendo.

---

## Українською (коротко)

Статична рекомпіляція Xbox 360 версії Castlevania: Harmony of Despair для Switch. Файли гри не додано: потрібна
власна копія XBLA-пакета.

**Обмеження:**
- ~40 fps (режим 60: 40–45 fps нерівно; режим за замовчуванням тримає рівні 30 fps). Швидше не вийде.
- Мультиплеєра немає: оригінал грає лише через Xbox LIVE.
- Тестування дуже мале: можливі графічні артефакти, вильоти, зависання, втрата збережень.
- Збірки після 0.300 зроблені з title update (потрібні `default.xexp`, `*.dllp` і тека `update/`) і вантажать усі
  DLC (розділ "DLC and the title update" вище: пакети як є в `sdmc:/switch/cvhd/install/`, або скрипт `tools/dlc_install.py`). Реліз 0.292/0.295 — з базової гри: глави 8–11 там вилітають.

Готові збірки для Switch, Windows і Linux: [Releases](https://github.com/rashevskyv/cvhd-recomp/releases). Вони містять
перекомпільований код гри, але без даних гри. Меню налаштувань порту на Switch: L3 + R3. Встановлення: розділ Install вище.
