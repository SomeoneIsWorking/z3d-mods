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

## The installer: the Python tools ship beside the app
User: "an installer or something that sets everything up" from their own files: the app plus
mods, no licensed mods shipped.

Decided: the package carries the tools (`tools/z3dmods`, its locked environment and a Python
runtime) beside the app, and the app runs them. The install is not rewritten in C++.
- One implementation. The install is 3000 lines that are already proven on both games: BPS and
  IPS, the stepped rooms (CMB and ZSI rewriting, the collision budget), the menu textures (font
  and SVG rendering), the grown exheader, the hook checks. A C++ copy would be a second
  implementation of every one of those rules until the Python is deleted, and the tools
  (`run`, `inputtest`, the decomp readers) need the Python readers anyway.
- It runs once per game, for minutes, on files on disk. Nothing in it is on the frame path, so
  C++ buys the player nothing.
- The cost is package size: a Python runtime and the wheels (Pillow, the SVG and font
  renderers), tens of megabytes beside a core library of that order.
- The GPL does not reach it: the tools are a separate program the app starts, MIT like the rest
  of our files.
The C++ rewrite stays the fallback if a platform cannot carry a Python runtime (Android).

### Slices
1. Built: the picker sets up a game whose set is missing (below).
2. The installer takes the player's mod folder instead of `env-assets/`: a folder picker in the
   app, each download found by the set's SHA-256 (`source_check.py`), a missing or different
   one named on the setup screen.
3. Our patch ships prebuilt: `code.ips` and its hook table built at release for each set's base,
   so the player's machine needs no ARM compiler (`patch_build.py` runs one today).
4. The package: the app, the core library, the tools with a relocatable Python
   (`uv python install` into the package, `uv sync --frozen`), port-assets' glyphs and fonts;
   the ROM folder asked for on first run (`first-run-setup`).

### Slice 1 (2026-11-14)
- `app/src/set_install`: a game's set is missing when its options manifest
  (`Azahar/load/mods/<title id>/z3d-options.json`) is not in the user folder. The app is told
  the installer with `--installer <program> [<argument>]...` (the rest of the command line) and
  runs it with the dump's path added, as a child process whose printed lines it reads.
- The picker's bottom line for such a game reads "A  Set up from your game". A opens a
  screen of its own, "Setting up <title>" over the installer's last line; when the installer
  ends with the manifest in place the game boots with its options read again, otherwise the
  screen says "Setup failed" over the last line until A.
- `z3dmods setup --user <folder> <dump>` (`tools/z3dmods/setup.py`) is that installer: it finds
  the set by the dump's title ID and runs `install` on that dump into `<folder>/Azahar/load`.
  `z3dmods play` passes it to the picker. The Ally's copy has no tools, so its picker gets none.
- The mods and the pack still come from `env-assets/` and the patch is still compiled there.

## Not built
Hotswap of `romfs/` files and of a pack changed on disk needs the core to drop what it listed at
load.

## Open
- Hidden-window mode for tools (the current harness is already that).
- Input: real host events for the player; the tool path keeps the `input` command.
- The openSU agent is told when the app exists.
