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
layout"): pieces of the game's own touch screen drawn on the top screen from a table,
`patch/mm3d/overlay.toml`, by which touch screen is up (`gUiScreenContext.activeScreen`,
mm3d-decomp `docs/ui.md`): the HUD's elements one by one in play, the whole screen in a menu. The
user rejected the first form, the whole touch screen as one inset with its empty middle panel.
It is still the game's touch screen composited; the HUD drawn by us is plans/pc-controls-hud.md.

The user rejected the play elements cut out of the touch screen as well (2026-11-14: "do this
properly RE based instead of copying patches of bottom screen"). Play gets our own HUD drawn from
the game's state (plans/pc-controls-hud.md, "Rule"); the cut-out pieces are deleted when it lands.
The menus showing the touch screen whole is a stopgap.

Done for play (2026-11-14): `patch/mm3d/overlay.toml`'s `play` layout has no pieces; the HUD is
`patch/mm3d/hud.toml` (`mm-hud2`). What it does not draw yet is listed in plans/pc-controls-hud.md.

Seen (`mm-ov3`): play has the elements along the edges, Items and Masks are large, the area's
title card draws the top screen alone (`hudVisibility` 2). The pad reaches the touch-only buttons
through Project Restoration: ZL is View, ZR with X is item I and ZR with Y is item II (`mm-zl`,
`mm-zr3`).

The Bombers' Notebook (table entry 2) is side by side: its list is on the top screen and the entry
with Return and Schedule on the touch screen (`mm-pad2`). The ocarina's screen is an inset for as
long as it is up (`mm-oc3`): the game sets the HUD visibility that hides the main screen while
Link plays, and only the main screen follows it.

Left:
- The Song of Soaring map and the Pictograph Box have not been seen in this layout.
- The map tabs have no pad button of their own; Project Restoration opens Items with Select, the
  notebook with Start, Gear with ZR and Start, the map with ZR and Select and the ocarina with ZR
  and A (`mm-pad1`).
- The minimap is not drawn in play: the panel it shares is empty without the area's map, and what
  says the map is owned is not found yet. The black box under Gear is not drawn either; what it
  shows is not known.
- The hearts keep the touch screen's dark strip behind them until the HUD is ours.

Fallback: an MIT code patch that moves the bottom-screen UI (items, masks, map, notebook, Ocarina,
file select) onto the top screen in the game's own code, in the same `code.ips` as the skip, on
Project Restoration's v1.0-layout `code.bin`, built from mm3d-decomp and PR's source (GPL, evidence
only). It does not fork Azahar.

## OoT3D (fallback only)
If The One Screen cannot be used, the same approach for OoT3D, built on zeldaret/oot3d (CC0), with
the OoT3D Randomizer's loader as evidence only (no licence for its own code), in the same `code.ips` as the skip and Quick Text.
