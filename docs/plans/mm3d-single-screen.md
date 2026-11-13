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
touch screen composited, not a redrawn HUD.

Seen (`mm-ov-final`, `mm-ov-screens`, South Clock Town): the area's title card and its cutscene
draw the top screen alone (`hudVisibility` 2), play has the inset, Gear, Items and Masks are large,
the map puts the two screens side by side with the map itself on the top one, the ocarina keeps
the top screen. The pad reaches the touch-only buttons through Project Restoration: ZL is View,
ZR with X is item I and ZR with Y is item II (`mm-zl`, `mm-zr3`).

Left:
- The Bombers' Notebook (table entry 2, side by side until it is looked at), the Song of Soaring
  map and the Pictograph Box have not been seen in this layout.
- The ocarina button and the map tabs have no pad button; Start and Select open Gear and Items.
- A redrawn HUD (hearts, rupees and buttons as their own elements on the top screen) needs the
  game's layouts drawn by the app or moved by a patch.

Fallback: an MIT code patch that moves the bottom-screen UI (items, masks, map, notebook, Ocarina,
file select) onto the top screen in the game's own code, in the same `code.ips` as the skip, on
Project Restoration's v1.0-layout `code.bin`, built from mm3d-decomp and PR's source (GPL, evidence
only). It does not fork Azahar.

## OoT3D (fallback only)
If The One Screen cannot be used, the same approach for OoT3D, built on zeldaret/oot3d (CC0), with
the OoT3D Randomizer's loader as evidence only (no licence for its own code), in the same `code.ips` as the skip and Quick Text.
