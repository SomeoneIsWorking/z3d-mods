# Azahar hangs when a new MM3D file asks for a name

Observed 2026-11-13 on the player's standalone Azahar 2126.2 (in the tools' headless Gamescope):
A on "New Game" in MM3D's file select starts the software keyboard applet (title
`0004003000 00C802`). Azahar logs `Loader <Error> ... GetLoader: Failed to load file
.../nand/.../title/00040030/0000c802/content/00000000.app` and stops: no dialog, no frames, RPC
does not answer.

Expected: a name prompt, then "Save completed." and the opening.

Cause: `[System] lle_applets=true`, Azahar's default (`lle_applets\default=true` in
`qt-config.ini`), makes Azahar run the system's own applets from NAND, and the player's NAND
holds no system titles. With `lle_applets=false` for one run, Azahar's built-in Qt keyboard dialog
opens, takes the name, and the game shows "Save completed." and starts the opening. Any game that
opens the keyboard (an OoT3D new file too) is affected the same way.

Fix: the player's choice, since it is a global setting: set `[System] lle_applets=false` in
`qt-config.ini` (Azahar's LLE applets option), or install the system applets from their own console's
NAND dump. The sets do not change it. The harness's libretro core answers the keyboard itself and
never shows this.
