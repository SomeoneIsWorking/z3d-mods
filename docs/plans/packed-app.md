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
5. Built: the first run asks for the dump folder and the mods folder in the window.
6. Built: the menu textures' glyphs ship drawn, so a setup needs no ImageMagick.
7. Built: a set made by another package is made again.
8. Built: the AppImage of the package folder.

### The picker's setup
- The app is told the tools with `--tools <program> [<argument>]...` (the rest of the command
  line) and runs them as child processes whose printed lines it reads (`app/src/tool_process`).
- `app/src/set_install`: a game wants a setup when its options manifest
  (`Azahar/load/mods/<title id>/z3d-options.json`) is missing from the user folder, or names
  other inputs than the library does ("A set made by another package" below). It runs
  `<tools> setup --user <folder> <dump>`.
- The picker's bottom line for such a game reads "A  Set up from your game", or "A  Set up
  again for this version". A opens a screen of its own, "Setting up <title>" over the setup's
  last line; when it ends with the set in place the game boots with its options read again,
  otherwise the screen says "Setup failed" over the refusal (the lines that start with "! ")
  until A.
- `z3dmods setup --user <folder> <dump>` (`tools/z3dmods/setup.py`) finds the set by the dump's
  title ID and runs `install` on that dump into `<folder>/Azahar/load`.

### The first run
- A package's launcher lists the dumps in the folder `<user>/folders.json` names. With none
  (no file yet, an empty or unplugged folder) the library it writes is empty and the picker
  opens the folder questions instead of its tiles (`app/src/folder_setup`): the dump folder,
  then the mods folder.
- Each is the shared setup screen (`shared/setup-ui`, one requirement, the player's own
  location adopted) drawn with the picker's renderer; its button opens the app's folder browser
  (`app/src/folder_browser`), which a pad, the keyboard or the mouse walks. No system dialog,
  no terminal.
- The tools take the answer: `<tools> folders --user <folder> --roms <folder>` refuses a
  folder without a dump of either game, and otherwise keeps it in `folders.json` and writes the
  library; `--mods <folder>` keeps the mods folder. The app then reads the library and opens
  the picker.
- `z3d --roms <folder> --mods <folder>` still sets them from a terminal, for a maintainer.
- The picker's options end in "Games and mods folders", which asks both again, each showing
  the folder set now; a question dismissed keeps its folder.

### The player's mod downloads
- A set names each third-party source by what it is, where to get it and its SHA-256 (`"from"`
  and `"sha256"` under `base` and `textures`), never by a path. `source_check.py` looks through
  one folder for them: the base mod is the folder holding the set's code patch whose every
  entry has the set's digest, the texture pack the folder named after the title ID with the
  set's digest. A `.zip` or `.tar` left in the folder is unpacked beside itself first; a `.7z`
  or `.rar` is named for the player to unpack.
- The folder is `env-assets/downloads` in a checkout, and in a package the one the first run
  asked for (`"mods"` in `folders.json`), `<user>/mods` without one.
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
  (no function map), and `patch/<game>/*.toml`. Both decomps are public.

### The drawn glyphs
- `hud.py` draws port-assets' pad glyphs over the 3DS button prompts of the menu textures.
  `glyphs.py` gives it each glyph as a 1024-pixel image, which it scales down: a checkout has
  ImageMagick draw the SVG on first use (`build/glyphs/`), `z3dmods package` draws the whole
  set into `prebuilt/glyphs/`, and a package reads those. One drawing path; the player's
  machine runs no ImageMagick.

### A set made by another package
- `set_inputs.py` digests what of ours a set is made from: `sets/`, the patch
  (`patch/<game>`, `patch/common`, and in a package `prebuilt/<game>`), the drawn glyphs and
  the tools, and the app's key bindings (`z3d --keys`), which the text's key caps are drawn
  from. The install writes it into the options manifest as `"inputs"`.
- A package's launcher writes the digest it would get now into each library entry. The picker
  compares the two (`SetInstall::state`): a set with another digest, or none, reads "A  Set up
  again for this version" and A makes it again over the old one. The saves are not touched.
- A checkout's library names no digest: its sets are the maintainer's to install.

### The package
`z3dmods package` makes `build/package/z3d` (`tools/z3dmods/package.py`, 171 MB) and
`build/package/z3d-x86_64.AppImage` (53 MB):
- `z3d`, the launcher; `bin/z3d`; `lib/`: the core and every library the app links (SDL3,
  SDL3_ttf, SDL3_image, freetype, harfbuzz, png and what they need) but the C and C++
  runtimes. Vulkan is the desktop's loader, which the core opens itself;
- `python/`, uv's relocatable CPython 3.12 with the locked wheels (Pillow) in its
  site-packages, and `tools/z3dmods/` on its path: no system Python, no uv;
- `sets/`, `patch/<game>/*.toml`, `prebuilt/<game>/`, `prebuilt/glyphs/`, `assets/`
  (port-assets' sets and key typeface), `fonts/` (the picker's), `LICENSE`;
- `SOURCE.txt`, the GPL's source: the commit of this repository and of every checkout the build
  was made from (`tools/z3dmods/sources.py`: the core, Lucent, setup-ui, RmlUi, port-assets),
  marked "with uncommitted changes" where a checkout's tracked files differ from it;
- `licenses/`: the core's GPL and the licences its externals carry, Lucent's, setup-ui's and
  RmlUi's, the SDL libraries' as the image installed them, each other library's Ubuntu
  `copyright` file (by the package that owns the file), CPython's, and a `README.txt` that
  says which is which;
- `AppRun` (a link to the launcher), `z3d.desktop` and `z3d.svg`, which make the folder an
  AppDir;
- `package.json`, which tells `config.py` this is a package.
The launcher runs `python -I -m z3dmods.packed play`: it lists the dumps in the player's
folder that a set is made for, writes the library and starts the app with `z3dmods.packed` as
its tools.

The two forms are the same files:
- The folder keeps the player's data in `user/` inside it when that exists, so one folder
  holds everything (the Ally's install, which `z3dmods shortcut --host ally` updates with
  rsync, sending only what changed).
- The AppImage is the folder as a squashfs image behind the AppImage runtime
  (`AppImage/type2-runtime` release 20251108, pinned by SHA-256, kept under
  `<dependencies>/appimage-runtime/`): one file to download and start. It is read-only, so the
  player's data is `~/.local/share/z3d`. It needs FUSE on the desktop (fusermount3), or
  `--appimage-extract-and-run`.

The app and the core in it are built in a container (`packaging/Containerfile`,
`tools/z3dmods/build_container.py`; podman), so the package runs on desktops older than the
maintainer's:
- The image is Ubuntu 24.04 with Clang 22 (apt.llvm.org), CMake 4.3 and SDL3, SDL3_ttf and
  SDL3_image built from their release tarballs, each pinned by SHA-256. It is tagged by the
  recipe's hash and built once.
- The checkout, the core's source and the shared checkouts (Lucent, setup-ui, RmlUi) are
  mounted at their own paths; the core's prefix is `<dependencies>/azahar-core/<revision>-
  clang-ubuntu24.04`, the app's tree `build/package-app`. The checkout's own build
  (`build/app`) is not what a package holds.
- The libraries in `lib/` are the ones the app links there, copied out of the container.
- The build names no folder of the maintainer's machine: source files are compiled under
  their checkout's name (`-ffile-prefix-map`), the app has no run path (the launcher names
  `lib/`), and Python's build variables get back the prefix its distribution ships. The build
  refuses a package in which any file holds the home folder's path, the dump folder's or the
  downloads', or is Python bytecode (`package._check_private`).
- What a desktop must have: glibc 2.39 and GCC 13's libstdc++ (Ubuntu 24.04, Debian 13,
  Fedora 40 or newer), a Vulkan driver, and the X11 or Wayland libraries SDL3 opens. The
  build refuses a package in which any program or library needs a newer glibc.
- 24.04 is the floor because Lucent's log uses `<format>`, which GCC 12's libstdc++ (Debian
  12, Ubuntu 22.04) lacks.

### The release
- A release is a tag `v<version>` (`pyproject.toml`'s) with `z3d-x86_64.AppImage` and its
  `.sha256` on its GitHub Release, and notes in `docs/releases/<tag>.md`. The README links the
  latest one.
- `v0.1.0` was published from the maintainer's build: a clean worktree of the tagged commit,
  `z3dmods package`, then `gh release create`.
- `.github/workflows/release.yml` runs the same `z3dmods package` on a hosted runner, started
  by hand (the AppImage is the run's artifact) or by a `v*` tag (it attaches the AppImage to
  the tag's release, making the release from the notes file when there is none; it builds
  nothing when the release already holds an AppImage). `z3dmods sources` gives the runner the
  checkouts at the revisions `sources.py` pins, so bump those to what a release's `SOURCE.txt`
  names. It needs no game data and no mod: a package is made of none. The runner's Ubuntu has
  ImageMagick 6, so it draws the glyphs with Fedora's in a container
  (`packaging/Containerfile.glyphs`).
- Not run on a runner: anything that needs a dump (the setup, the input tests, the harness).
  Those stay on the maintainer's machines.

### Seen (2026-11-14)
- The release, `v0.2.0` (commit 587af4b): built by the tag's workflow run (43611544851), which
  made the release from `docs/releases/v0.2.0.md`. The downloaded AppImage matches its
  `.sha256`. Unpacked, it holds 1436 files. None of them holds a home path, a dump or a mod's
  file. The two `code.ips` files are our patches, and `sets/` is byte for byte the tagged
  commit's. On the Ally, on an empty data folder told the dump folder, it opens the picker with
  both tiles. `z3dmods shortcut --host ally`, run from a clean worktree of the tag, put the same
  package in Games/z3d after backing up its user folder. Its launcher opens the same picker.
- The release, `v0.1.0` (commit 0d77076): the AppImage downloaded from the release has the
  build's SHA-256; on the Ally, with an empty data folder and the dump folder, it opens the
  picker, and unpacked it sets Majora's Mask 3D up with its own tools and boots it to the
  title's Clock Town scene with the 4K pack. In the older-desktop container it opens the picker
  with both tiles. No file in it holds a home path, a dump or a mod.
- The workflow, started by hand on 7930b78 (run 43592663222, 20 minutes): its AppImage holds
  the same 1417 files as the maintainer's build of that source; `bin/z3d` and both patches
  are the same bytes, the glyphs the same pixels; the core's library and the picker's font
  (Ubuntu's Noto Sans) differ, and it opens the picker on the Ally. The tag's run found the release's AppImage and built nothing.
- The older desktop, `z3dmods inputtest appimage-older-desktop`: the AppImage started in a
  container of Ubuntu 24.04 that holds only a desktop's libraries
  (`packaging/Containerfile.desktop`, `tools/z3dmods/desktop_container.py`; no network, the
  test's Gamescope as its display), told the dump folder: it unpacks itself and opens the
  picker with both games' tiles.
- The first run (`z3dmods inputtest first-run`, the package's launcher on an empty data folder,
  and `first-run-appimage`, the AppImage): docs/app.md, "Checking it".
- The Ally, `pk-ally4`, the container-built package: the AppImage on an empty data folder,
  told the dump folder, opens the picker with both tiles; the installed folder sets both games
  up again (their inputs changed) and boots Ocarina of Time 3D to file select and Majora's Mask
  3D to its title.
- The Ally, `pk-ally2`: the package installed by `z3dmods shortcut --host ally` over sets an
  older package made, The One Screen left in its mods folder as the `.tar.gz` its site gives,
  the launcher started in a headless Gamescope with only `dirname` and `readlink` on its PATH
  (no ImageMagick, no clang, no uv), real keys. Both tiles read "A  Set up again for this
  version". A on Ocarina of Time 3D shows "Unpacking the-one-screen-...tar.gz", the setup ends
  after 25 s and Start reaches file select with the player's "Link" file; A on Majora's Mask
  3D, 28 s, and its title with the 4K pack. Back on the picker the Ocarina tile reads "A  Play",
  and each game's manifest holds its library entry's `"inputs"`.
- The glyph textures of that run against the ones the older package made with ImageMagick on
  the Ally (16 files under `z3dmods-hud/` differ in bytes): MM3D's and OoT3D's HUD sheets side
  by side show the same pad glyphs in the same places.
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
