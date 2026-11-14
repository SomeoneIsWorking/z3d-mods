# Packed app (harness as the product)

User: "I usually prefer packing since I can add hotswap and such", "And like game specific options
menu", and "We can also make the harness itself an app with a custom menu maybe instead of shipping
this as a libretro thing, we ship it as an app + mods, for potential licensing issues, we could add
some like mod compatibility layers so we don't ship licensed mods, instead we add support for them
but for my own installation I want the mods to be here".

## What it is
A windowed app grown from `harness/`: it loads the Azahar core from our fork, runs one game, and
owns a per-game options menu (cutscene skip, quick text, layout, texture set) with hotswap of our
own patches and of texture packs.
- Single screen (MM3D first) is the app's: it owns the presented frames and the input, so it draws
  the bottom-screen UI on the top screen and maps input to it, written from facts (mm3d-decomp,
  Project Restoration and DekUI as evidence). No Qt.

## What ships and what does not
- Ships: the app, our MIT patches (`patch/`), and loaders that apply third-party mod formats
  (`code.bps`/`code.ips` with exheader, LayeredFS romfs, Azahar texture packs, legacy and new hash).
- Does not ship: third-party mods (The One Screen, Project Restoration, Omni Xbox UI, Henriko
  textures) and no game data. The player points the app at their own copies; the loaders check each
  one against the hashes in its set.
- The user's own install keeps all current mods: the app reads the same sets `z3dmods install` uses.

## Licensing facts
- User: "I don't like having any restrictions", then "this needs to be an app using Azahar
  libretro underneath". The app loads the Azahar core (GPL-2.0-or-later) in process, so a
  distributed build is a combined work under the GPL: it ships with the full source. Our own files
  stay MIT. GPL mods (DekUI, Project Restoration) are evidence only; their code is never copied.
- Third-party mods without a permissive license are never redistributed; the compatibility loaders
  avoid that by applying mods the player already has, not by bundling them.

## Status (2026-11-14)
- Built: the app (docs/app.md), the per-game options menu, and hotswap of our patch and of the
  texture pack in a running game (`app/src/mod_swap`, docs/app.md "Hotswap"; F5, F6 and F7 in the
  window).
- The loaders that exist are the core's: `exheader.bin`, `code.bin`, one `code.ips` or
  `code.bps`, `romfs/` and its layers, texture packs of either hash. The app adds none of its
  own; hotswap reuses the core's loader and IPS reader.
- Not built: the app checking a player's third-party files against a set's hashes and baking
  the base mod itself. `z3dmods install` (Python) still does both, so a shipped app would need
  that step in the app or the tools shipped with it. Hotswap of `romfs/` files and of a pack
  changed on disk needs the core to drop what it listed at load.

## Open
- Hidden-window mode for tools (the current harness is already that).
- Input: real host events for the player; the tool path keeps the `input` command.
- The openSU agent is told when the app exists.
