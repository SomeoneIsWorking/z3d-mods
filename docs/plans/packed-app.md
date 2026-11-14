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
- Each set names the SHA-256 of every third-party input it reads (`"sha256"` under `base` and
  `textures`; a folder's is the digest of its file listing, `tools/z3dmods/source_check.py`), and
  `z3dmods install` refuses a copy that differs before it touches the install. Shown: a file added
  to the MM3D pack's folder stops the install and leaves the installed set as it was.
- The base mod is baked by `z3dmods install` (`install._layer`: the base patch into `code.bin`,
  ours on top as `code.ips`), not by the app. What an install makes from the player's files is
  more than that bake: the stepped rooms (`stairs.py`), the menu textures (`hud.py`), the grown
  exheader and the check of every hook's original word. All of it is Python on game data, so
  none of it can ship prebuilt.

## To decide
A fresh install from the app alone needs that work reachable from the app. Two ways:
- The app ships the tools (the locked Python environment and `z3dmods`) and runs the install on
  first launch, from a folder picker for the ROM and the mods. Nothing is written twice; the
  package grows by a Python runtime.
- The install is rewritten in C++ inside the app (BPS and IPS, CMB and ZSI rewriting, the HUD
  composition with font and SVG rendering) and the Python is deleted. Months of work with nothing
  new for the player's own install.
Neither is started. Hotswap of `romfs/` files and of a pack changed on disk needs the core to drop
what it listed at load; not built.

## Open
- Hidden-window mode for tools (the current harness is already that).
- Input: real host events for the player; the tool path keeps the `input` command.
- The openSU agent is told when the app exists.
