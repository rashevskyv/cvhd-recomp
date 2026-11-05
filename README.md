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
>   six characters and most menus have not been checked. The title update is not supported. Of the DLC,
>   chapter 7, the two BGM packs and the six characters load (see DLC); chapters 8-11 crash the game.
> - **Graphical glitches are possible**, and so are **crashes and freezes**. Save data may be lost.
> - Use at your own risk. Quit with EXIT GAME in the port menu, not by resetting the console.

## Install

Download your platform from [Releases](https://github.com/rashevskyv/cvhd-recomp/releases). The release files contain
the recompiled game program, which is a derived work of the game, but no game data (no graphics, sound or text). Use
them only with a copy of the game you own. You can also build everything yourself (see Build).

First extract your XBLA package: `python tools/stfs_extract.py <package> <out>`. You need `default.xex` and the
`data/` folder (do not use `default.xexp`, the title update).

**Switch** (Atmosphère and hbmenu):
1. Unzip the Switch zip to the root of the SD card. It creates `sdmc:/switch/cvhd/` (`cvhd.nro`, `deko3d`, `shaders/`).
2. Copy `default.xex` and `data/` into `sdmc:/switch/cvhd/game/`.
3. Start as a title takeover: hold R on any game and launch "Castlevania HD". Applet mode is not tested.

Logs: `sdmc:/switch/cvhd/cvhd_log.txt` and `cvhd_rex_log.txt`.

**Windows 10/11 x64 and Linux x86-64** (a GPU with Vulkan 1.1):
1. Unpack the zip (Windows) or the tar.gz (Linux).
2. Put `default.xex` and `data/` into a folder `game/` next to `cvhd.exe` (Linux: next to `cvhd`).
3. Start `cvhd.exe` (Linux: `./cvhd.sh`). Or pass `--game_data_root=<folder>`.

The PC builds were only checked up to the title screen. They do not have the Switch settings menu.

## DLC

These DLC packages load: **Ch. 7 "Beauty, Desire, Situation Dire"** (Pyramid stage pack), **BGM Pack 1**, **BGM
Pack 2** and the six characters (**Simon, Richter, Maria, Julius, Yoko, Getsu Fuma**). The characters carry program
code (Xbox 360 DLLs) that the port recompiles from your own files at build time (`tools/dlc_modules.py`); the release
builds do not contain it. Playing a DLC character has had very little testing. **Chapters 8-11 do not work: with
any of them installed the game crashes about 40 s after start.** They probably need the title update, which the port
does not support yet.

You need your own DLC files: the STFS packages from an Xbox 360 hard drive (`Content/0000000000000000/58410A7A/00000002/`)
or a USB stick. Install them on a PC with Python 3, then copy the result:

1. Run, with one or more packages:
   ```
   python tools/dlc_install.py dlc_out "<path>/Ch07 package" "<path>/BGM pack 1" "<path>/BGM pack 2"
   ```
   The script checks that each file is a DLC package (content type 2), unpacks it and writes a header with the licence.
   It creates `dlc_out/0000000000000000/58410A7A/00000002/<name>/` and `dlc_out/0000000000000000/58410A7A/Headers/`.
2. Copy the folder `0000000000000000` from `dlc_out` into the port's user folder:
   - Switch: `sdmc:/switch/cvhd/user/` (create `user` if it does not exist).
   - Windows: `Documents\cvhd\`.
   - Linux: `~/.local/share/cvhd/` (or `$XDG_DATA_HOME/cvhd/`).
3. Start the game. To remove a DLC, delete its folder and its `.header` file.

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
- Title update не підтримується. З DLC працюють глава 7, два BGM-пакети і шість персонажів (розділ DLC вище,
  скрипт `tools/dlc_install.py`). Глави 8–11 не працюють: з ними гра вилітає приблизно через 40 с.

Готові збірки для Switch, Windows і Linux: [Releases](https://github.com/rashevskyv/cvhd-recomp/releases). Вони містять
перекомпільований код гри, але без даних гри. Меню налаштувань порту на Switch: L3 + R3. Встановлення: розділ Install вище.
