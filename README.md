# Castlevania: Harmony of Despair (Xbox 360) on Nintendo Switch

Static recompilation of the XBLA game for Switch. No game data is included: you need your own copy of the
XBLA package (`tools/stfs_extract.py` unpacks it).

- Release notes, what works and what does not: [docs/RELEASE-NOTES.md](docs/RELEASE-NOTES.md) (Ukrainian).
- Install: `sdmc:/switch/cvhd/cvhd.nro`, the empty marker file `deko3d`, `shaders/` and `game/`
  (`default.xex` + `data/`) next to it; launch as title takeover (hold R on a game in hbmenu).
- Controls: positional (Switch B = Xbox A, A = B, Y = X, X = Y), `+` = START, `-` opens the settings menu
  (frame rate 60 / 30, rumble, volume, fps counter, button icons, exit).

Notes for contributors and agents: [CLAUDE.md](CLAUDE.md).
