# Button glyphs on Switch (user request 2026-10-23)

Our Switch input is POSITIONAL: Xbox A (bottom) = Switch B, Xbox B (right) = Switch A, Xbox X (left) = Switch Y, Xbox Y (top) = Switch X. The game draws Xbox glyphs, so the screen says A where the Switch button at that spot is B. Redraw the on-screen glyphs: show B where the game shows A, A where it shows B, Y for X and X for Y (physical layout unchanged). Do it by swapping the glyph textures/characters in the game UI atlas or font at load time. Option button_glyphs=switch|xbox, default switch on Switch builds. Verify with an Eden screenshot of a prompt screen. Same task is being done in the Guardian Heroes and Hard Corps ports.

## Confirm/cancel in menus (user request 2026-10-23)

On Switch, confirm is conventionally the RIGHT button (Switch A) and cancel the bottom (Switch B). The games confirm with the bottom button (Xbox A / PS cross). Swap confirm/cancel IN MENUS only (gameplay stays), option menu_confirm=nintendo|xbox, default nintendo on Switch. Prefer wrapping the menu handlers input read; fall back to a global A/B swap and say so. Glyphs must match the active mapping.
