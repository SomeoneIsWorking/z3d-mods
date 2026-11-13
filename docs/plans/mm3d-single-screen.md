# Our own single screen (MM3D; OoT3D as fallback)

User: "single screen exists"; on the CC BY-NC-ND and fork routes: "I'd rather make MIT versions of
those then".

## MM3D
User, 2026-11-13: "Can't you copy the RE? Anyway, do what you can please", then on building
DekUI: "Look, no, no Qt".

DekUI (GPL-2.0) is not usable: it is its own Azahar fork whose single screen is host code in the
emulator (`src/mm3d/`, native inventory, file select, notebook and minimap drawn by the frontend),
and it builds only with the Qt or Android frontend. Its source (v1.1.0, `5cf063e`, read only) is evidence for which guest functions and state each menu uses;
its code is not copied.

User, 2026-11-13: "this needs to be an app using Azahar libretro underneath": single screen moves
to the packed app (docs/plans/packed-app.md), drawn by the app. The code-patch route below is the
fallback for plain Azahar.

Status (2026-11-14): the app's `overlay` layout is MM3D's set default (docs/app.md, "Overlay
layout"): the game's own touch screen drawn over the top screen, small in play and large in a
menu, placed from `gUiScreenContext.activeScreen` (mm3d-decomp `docs/ui.md`). It is the game's
touch screen composited, not a redrawn HUD. Left:
- The pad cannot press the touch-only buttons (items I and II, View, the map tabs' touch targets,
  the ocarina and Pictograph buttons); the mouse can. Chord item buttons (pad-features.md) cover
  I and II.
- Only Main and Items are checked in the screen table; the Bombers' Notebook, the Ocarina screen
  and the Song of Soaring map have not been looked at in this layout.
- A redrawn HUD (hearts, rupees and buttons as their own elements on the top screen) needs the
  game's layouts drawn by the app or moved by a patch.

Fallback: an MIT code patch that moves the bottom-screen UI (items, masks, map, notebook, Ocarina,
file select) onto the top screen in the game's own code, in the same `code.ips` as the skip, on
Project Restoration's v1.0-layout `code.bin`, built from mm3d-decomp and PR's source (GPL, evidence
only). It does not fork Azahar.

## OoT3D (fallback only)
If The One Screen cannot be used, the same approach for OoT3D, built on zeldaret/oot3d (CC0), with
the OoT3D Randomizer's loader as evidence only (no licence for its own code), in the same `code.ips` as the skip and Quick Text.
