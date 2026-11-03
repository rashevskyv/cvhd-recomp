# Castlevania: Harmony of Despair (Xbox 360) on Nintendo Switch

Static recompilation of the Xbox Live Arcade game for Nintendo Switch (homebrew NRO, libnx + deko3d), built on the
[ReXGlue SDK](https://github.com/rexglue/rexglue-sdk). **No game data is included.** You need your own copy of the
XBLA package (`tools/stfs_extract.py` unpacks it).

> **EXPERIMENTAL. READ THIS FIRST.**
>
> - **Performance is limited to about 40 fps.** In the 60 fps mode the game runs at 40 to 45 fps and the frame
>   pacing is uneven. The default mode locks the game to 30 fps. The CPU budget is used up: 60 fps is not
>   reachable with this approach on the Switch.
> - **No multiplayer.** The Xbox 360 game has online play through Xbox LIVE only and no local co-op. The port
>   cannot do either. Only one controller is used.
> - **Very little testing.** Only a few hours of single-player were played, on one console. Most stages, all
>   six characters and most menus have not been checked. Title update and DLC are not supported.
> - **Graphical glitches are possible**, and so are **crashes and freezes**. Save data may be lost.
> - Use at your own risk. Quit with EXIT GAME in the port menu, not by resetting the console.

## Install

**This repository ships no binaries.** The NRO contains code recompiled from the game, and the shaders are
translated from the game's own, so both are derived from Konami's copyrighted work and cannot be published here.
You build them from your own copy of the game (see Build).

1. Extract your XBLA package: `python tools/stfs_extract.py <package> <out>`.
2. Build `cvhd.nro` and the `shaders/` folder (see Build).
3. Copy to the SD card, folder `sdmc:/switch/cvhd/`: `cvhd.nro`, the empty file `deko3d`, `shaders/`, and `game/`
   with `default.xex` and `data/` (no `default.xexp`).
4. Start as a title takeover: hold R on any game and launch "Castlevania HD".

Logs: `sdmc:/switch/cvhd/cvhd_log.txt` and `cvhd_rex_log.txt`.

## Controls

Buttons are positional, as on the Xbox pad: Switch B = Xbox A, A = B, Y = X, X = Y. `+` = START, `-` = BACK
(the game's own menu). **L3 + R3** (press both sticks) opens the port's settings menu: frame rate (60 / 30 / 30 skip),
rumble, volume, fps counter, button icons (Switch / Xbox), exit game. Settings are saved in
`sdmc:/switch/cvhd/settings.txt`.

## Build

`switch/build.sh` in WSL (devkitA64 and libnx; the SDK patches are in `switch/sdk-edits.py`). You must supply the
game files yourself: the recompiled code is generated from your `default.xex` and is never stored in this
repository. Details: [CLAUDE.md](CLAUDE.md) (dated findings) and
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
- Title update і DLC не підтримуються.

Меню налаштувань порту: L3 + R3. Встановлення: див. розділ Install вище.
