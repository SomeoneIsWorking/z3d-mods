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
1. Built: the picker sets up a game whose set is missing.
2. Built: the installer reads the player's own mod downloads, found by the set's hashes; a
   missing or different one is named on the setup screen with where to get it.
3. Built: our patch ships prebuilt, so the player's machine needs no ARM compiler.
4. Built: the package folder (`z3dmods package`), installed on the Ally by
   `z3dmods shortcut --host ally`.
5. Not built: the folders asked for in the window. Today the dump folder and the mods folder
   are given once on the command line (`z3d --roms <folder> --mods <folder>`) and kept in
   `<user>/folders.json`; without a dump folder the launcher stops with a line on the terminal,
   which a desktop start never shows. Next: the picker's own first-run screen
   (`first-run-setup`), a folder dialog for each.
6. Not built: the menu textures' glyphs are rasterized by ImageMagick (`hud.py` `_glyph`), the
   one program a setup still needs from the system; it refuses with the install command when
   `magick` is missing. Next: rasterize them in the tools.
7. Not built: a set made by an older package is kept as it is: the picker asks for a setup
   only when the options manifest is missing, so a package with a changed patch runs the old
   one until the set is removed. Next: the install writes the package's revision into the
   manifest and `SetInstall::wanted` compares it with the one the launcher passes.
8. Not built: an AppImage. The folder is the package; the desktop's own libraries serve
   everything but the core, SDL3 and plutosvg (freetype, harfbuzz, png, Vulkan).

### The picker's setup
- `app/src/set_install`: a game's set is missing when its options manifest
  (`Azahar/load/mods/<title id>/z3d-options.json`) is not in the user folder. The app is told
  the installer with `--installer <program> [<argument>]...` (the rest of the command line) and
  runs it with the dump's path added, as a child process whose printed lines it reads.
- The picker's bottom line for such a game reads "A  Set up from your game". A opens a
  screen of its own, "Setting up <title>" over the installer's last line; when the installer
  ends with the manifest in place the game boots with its options read again, otherwise the
  screen says "Setup failed" over the installer's refusal (its lines that start with "! ")
  until A.
- `z3dmods setup --user <folder> --mods <folder> <dump>` (`tools/z3dmods/setup.py`) is that
  installer: it finds the set by the dump's title ID and runs `install` on that dump into
  `<folder>/Azahar/load`. `z3dmods play` passes it to the picker.

### The player's mod downloads
- A set names each third-party source by what it is, where to get it and its SHA-256 (`"from"`
  and `"sha256"` under `base` and `textures`), never by a path. `source_check.py` looks through
  one folder for them: the base mod is the folder holding the set's code patch whose every
  entry has the set's digest, the texture pack the folder named after the title ID with the
  set's digest. A `.zip` or `.tar` left in the folder is unpacked beside itself first; a `.7z`
  or `.rar` is named for the player to unpack.
- The folder is `env-assets/downloads` in a checkout, and in a package `<user>/mods` or the
  one given with `--mods`.
- Refusals are written for the player: a file that is not a decrypted dump, a dump of another
  revision ("It needs Majora's Mask 3D (USA) (Rev 1)"), a mod that is missing, a mod with a
  file that differs.

### The prebuilt patch
- A set says where its patch's payload starts (`"payload"`: the page after the base mod's
  `.bss`); the install refuses a base exheader that leaves another address.
- `z3dmods package` builds each patch for that address and saves it as
  `prebuilt/<game>/code.ips` and `hooks.json` (the payload's range and the base code each hook
  replaces). In a package `patch_build.patch` reads those; the install checks every hook's
  word against the player's baked code as before.
- The tools read guest names from the decomp, which a package does not carry: it has
  `prebuilt/<game>/symbols.json`, the decomp's globals, field offsets, sizes and defines
  (no function map), and `patch/<game>/*.toml`. For MM3D that table comes from mm3d-decomp,
  which is still private: publish it, or cut the table to the names the tools read, before a
  package goes public.

### The package
`z3dmods package` makes `build/package/z3d` (`tools/z3dmods/package.py`, 160 MB):
- `z3d`, the launcher; `bin/z3d`; `lib/` (the core, SDL3, SDL3_ttf, SDL3_image, plutosvg);
- `python/`, uv's relocatable CPython 3.12 with the locked wheels (Pillow) in its
  site-packages, and `tools/z3dmods/` on its path: no system Python, no uv;
- `sets/`, `patch/<game>/*.toml`, `prebuilt/<game>/`, `assets/` (port-assets' sets and key
  typeface), `fonts/` (the picker's), `LICENSE`, `SOURCE.txt` (the GPL's source offer);
- `package.json`, which tells `config.py` this is a package. The player's data is `user/`
  inside the folder when that exists (the Ally), else `~/.local/share/z3d`.
The launcher runs `python -I -m z3dmods.packed play`: it lists the dumps in the player's
folder that a set is made for, writes the library and starts the app with
`z3dmods.packed setup` as its installer.

### Seen (2026-11-14)
- The Ally, the package installed by `z3dmods shortcut --host ally`, its launcher in a headless
  Gamescope with real keys, the sets moved out of `user/` first: both tiles read "A  Set up
  from your game"; A on Majora's Mask 3D shows "Setting up", the installer ends after 32 s and
  the game boots to its title with the 4K pack; A on Ocarina of Time 3D, 26 s, and Start
  reaches file select with the player's "Link" file; the picker then reads "A  Play" for both.
  That machine has no clang, no uv and no checkout.
- The failure screen (`z3dmods inputtest setup-wrong-dump`, `setup-missing-mod`,
  `setup-different-mod`): "Setup failed" over "... is not the dump this setup was made for /
  It needs Majora's Mask 3D (USA) (Rev 1)"; over "Project Restoration ... is missing from your
  mods folder / Get it from <its releases page> / Unpack it into <the folder>"; over "... is not
  the version this setup was made for / Different: exheader.bin". A goes back to the tiles.

## Not built
Hotswap of `romfs/` files and of a pack changed on disk needs the core to drop what it listed at
load.

## Open
- Hidden-window mode for tools (the current harness is already that).
- Input: real host events for the player; the tool path keeps the `input` command.
- The openSU agent is told when the app exists.
