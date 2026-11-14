# Mods: inventory and the installed sets

What exists for OoT3D (USA, `0004000000033500`) and MM3D (USA, `0004000000125500`) on Azahar,
what we install and why. Checked 2026-11-12 against each project's source or release and against the
player's own dumps.

## The player's dumps

| Game | Product | Exheader remaster | `code.bin` sha256 (decompressed) | Revision |
|---|---|---|---|---|
| OoT3D | `CTR-P-AQEE` | v0 | `ef210566e1d9d16879a746dfb063fcbad232f0171d860de906531ecc526cc020` | USA Rev 1 executable: `.text` ends at 0x4C99A8, where ReFramed's and The One Screen's USA links place their loaders |
| MM3D | `CTR-P-AJRE` | v2 | `6eec27b1827eab2c87744ac38066d569ffe6fe75790b1eb3300bd91f5099038d` | USA 1.1: main partition (NCSD partition 0, 654643200 bytes) sha256 `9c756279…23da`, exactly DekUI's `supported-rom.json` |

No update title is installed for either game, and none of the chosen mods needs one. The player's
earlier MM3D 1.0 dump (remaster v0, code `050a5883…`) was deleted at their request on 2026-11-12;
its NCCH had crypto flags set over plaintext, which is why `tools/z3dmods/ctr.py` checks the
plaintext ExeFS header instead of the flag.

`uv run --frozen z3dmods rominfo <rom.3ds>` prints these.

## How Azahar 2126.2 stacks mods

From `src/core/file_sys/ncch_container.cpp` and `src/core/loader/ncch.cpp` at tag 2126.2:

- `load/mods/<tid>/exheader.bin` replaces the exheader (one per title).
- `code.bin` (`exefs/code.bin`, then `code.bin`) replaces the decompressed `.code` section.
- Then exactly one code patch is applied to the loaded image, the first found of `exefs/code.ips`,
  `exefs/code.bps`, `code.ips`, `code.bps` and the Luma path. It is applied after `.bss` is
  allocated, so a patch may write past the end of `code.bin` into `.bss`.
- `romfs/` overrides merge file by file; `load/textures/<tid>/` is scanned for custom textures.
- Per-game settings (`config/custom/<tid>.ini`) leak: BootGame calls `config->Save()` after loading
  them, and a setting under a per-game override writes its per-game value into `qt-config.ini`. One
  launch of a game with a per-game Single Screen layout makes every game single screen. So the sets
  never write per-game files; a set names its Azahar settings in `azahar_global`.

So two code mods cannot both be patches, but a base mod can be baked into a locally built `code.bin`
(the vanilla code with its patch applied, never committed) and one more mod can ride on top as
`code.ips`. That is how our own patches stack on the base sets without linking their code.

## Installed sets

`uv run --frozen z3dmods install oot3d mm3d` installs from `sets/<game>.json`. A set names each
third-party source by its SHA-256 and where to get it, not by a path: the install finds the copies
with those hashes under `env-assets/downloads/` (`source_check.py`; a package's setup looks in
the player's mods folder the same way, and `Z3D_MOD_SOURCES` names another folder for a test). It checks the ROM's
`code.bin` hash, writes `z3dmods-install.json` beside the files it wrote, and on reinstall removes
only those (refusing a folder with files it did not write, or a file changed since).

### OoT3D: The One Screen

| | |
|---|---|
| Source | github.com/toadalle/The-One-Screen, commit `891b60e364cc660b2d1edfd7916ec01e3bb0cadf` (alpha 1.1.12); GameBanana 724248 |
| Licence | MIT for its own code and tools; game-derived assets and inherited headers excluded (`THIRD_PARTY_NOTICES.md`). GameBanana asks for permission before modifying: our changes stay in our own patch, not in its files |
| Gives | Single screen (HUD, items, gear, map, Ocarina, file select on the top screen), Xbox-style button labels, LB/RB items, X/Y slot swap, D-pad shortcuts (Up Items, Down Gear, Right Ocarina, hold Left View), Start pause, Select map |
| Input | In play and in the pause screens it trades the pad word's X and Y bits before the game reads them (`source/input/policy.h`, `Input_Translate`): the 3DS Y presses the game's X item button and the 3DS X its Y. On the harness `x` is the 3DS X, the game's Y; our item slots press the game's buttons inside the patch, after the trade. |
| Not used | Its C-stick orbit camera (`source/camera/camera.c`, not in its README; it runs from the hook at 0x452434, after `Play_Update`). Our patch puts the retail branch back there and runs our own free camera, which has the options: two orbits would fight over the same eye |
| Layout | `exefs/code.ips` (installed as `code.ips`), `exheader.bin` (text rounded to 0x3CA000, `.bss` 0x6C3E8 → 0xF1000; payload at 0x610000–0x64B8B1), `romfs/menu/01_US_ENGLISH/*.ctxb`, `romfs/message/parts.ctxb` |
| Download | `env-assets/downloads/the-one-screen-891b60e…/` tarball sha256 `93c9ff9ab9f8c1080c37a9f2395bda336802227d76b3bc3699348ff088e68a79`; `code.ips` `10cf910a…a8026`, `exheader.bin` `81d21ba8…08740` |

Screen layout: The One Screen draws everything on the top screen but the game still renders its
bottom screen, so the player chooses Azahar's Single Screen layout (View › Screen Layout, or the
layout hotkey). The set does not set it, because of the per-game leak above.

| | |
|---|---|
| Textures | Henriko Magnifico OoT3D 4K 5.0a, 1080p (4x) variant: henrikomagnifico.com/zelda-ocarina-of-time-3d-4k, Google Drive file `1w1Ez_V2ky56Kc-Gz18cH3VnpraxiOD0Q`, zip sha256 `d68ad55f3067a0a89e21bab2fe5ae37e0847ff74b086965da6b0ed34b7693983` (1.9 GB) |
| Licence | None stated; free, "as is" |
| Layout | `user/load/textures/0004000000033500/` (2162 files, legacy hash: `pack.json` `use_new_hash: false`, `flip_png_files: true`) into `load/textures/0004000000033500/`. The zip also carries its own `load/mods` (code and romfs for RTG's single-screen mod); not installed, The One Screen is the base |
| Needs | `[Utility] custom_textures=true` (the set's `azahar_global`); the pack's guide says to keep Preload Custom Textures off, which the player's config already has |
| Shown | Harness, same state with textures on and off at 3x: the house sign and wood are the pack's, The One Screen's Xbox HUD labels are unchanged (no overlap) |

Hooks it owns (our patch must not touch them): 0x452390 and 0x452434 (before/after the play update),
plus the 44 render, HUD and input sites in its `source/generated/linker.ld`.

### Azahar settings both sets change
Global keys in `qt-config.ini`, written by `z3dmods install` from each set's `azahar_global`:

| Key | Why |
|---|---|
| `[Utility] custom_textures=true` | the texture packs load only with it |

Applets stay at Azahar's default (`lle_applets=true`). The player's NAND has no system applets, so a
new file's name keyboard (`00040030/0000C802`) fails to load and Azahar falls back to its own keyboard
dialog (`apt/applet_manager.cpp`); shown 2026-11-13, a new MM3D file named that way reached "Save
completed.". In the tools' headless session that dialog waits for `type:<text>`.

### MM3D: Project Restoration v110

| | |
|---|---|
| Source | github.com/leoetlino/project-restoration v1.5.9, `mm3d_project_restoration_v1.5.9-0-ge099fcc.7z`, sha256 `c575c1beeae8f7c68fc1126061b0f70d29cd9d0f5dd5697f53b83835638eca32` |
| Licence | GPL-2.0 |
| Gives | Zora fast swim, Deku hops, D-pad transformations, instant Bomber's Notebook, Song of Time saving option, boss rebalances, Inverted Song of Time at 1/3 speed, fewer song replays, Bombers no longer interrupting, softlock fixes |
| Layout | `v110/citra/code.bps` and `v110/citra/exheader.bin` for the player's Rev 1 (1.1) dump; the BPS source and target CRCs check against it. The patched code it outputs is laid out like 1.0 (PR rebuilds 1.1 into its 1.0-based code), so our MM3D hooks use 1.0/mm3d-decomp addresses; the installer checks each hook's original word against this bake (0x174374 `bl 0x1846B4` there, not in vanilla 1.1). |

### MM3D: Henriko's 4K pack

| | |
|---|---|
| Textures | Henriko Magnifico MM3D 4K 3.0b-1, 1080p variant: henrikomagnifico.com/zelda-majoras-mask-3d-4k, Google Drive, `MM 3D 4K 3.0b-1 (1080p).zip`, sha256 `ceaee06385ab09231dd9fde7813d055492dda942e95ca3b4486aa05c5b7e59d4` (1.7 GB). The 4K variant (Drive file `1dvDvcKDXuW2pBQI7r1ZX5n3B8_QmqInZ`, 3.7 GB) is not used |
| Licence | Free; none stated |
| Layout | `MM3D 4K 3.0b (1080p)/user/load/textures/0004000000125500/` (1625 files; `pack.json`: `use_new_hash: true`, `flip_png_files: true`) into `load/textures/0004000000125500/`. Only that folder is extracted; the zip's ReShade installer and shaders are never run or installed |

Its `use_new_hash` names each file by the hash of the texture's bytes as the game holds them, not
of the decoded texture as the OoT3D pack does (`tools/z3dmods/pica_texture.py`); `pack.json` goes in
with the pack, so every texture in the folder, ours included, is named that way.

The button prompts are our own: `sets/hud/mm3d-usa-rev1.json` draws port-assets' Xbox glyphs over
the 3DS prompts on the pack's copies of eight menu textures (plans/xbox-glyphs.md), installed under
`load/textures/0004000000125500/z3dmods-hud/`. Project Restoration edits no texture, so each of ours
has the name of the pack's copy, which the install leaves out: Azahar keeps one texture per hash.

MM3D single screen is not installed yet. DekUI and Majora ReMasked are GPL-2 forks of Azahar, not
mods for the player's Azahar 2126.2; DekUI draws its single-screen menus in the emulator and builds
only with Qt, which the player declined. Our MIT patch is planned in
[plans/mm3d-single-screen.md](plans/mm3d-single-screen.md).

## Everything else considered

| Mod | Game | Source, version | Licence | Layout | Status |
|---|---|---|---|---|---|
| Ocarina ReFramed | OoT3D | github.com/OTPR26/OOT3DHud v2.5.1 (2026-10-29), Single and Dual | GPL-3.0-or-later; free-cam base from Roberto-Nessy with no licence | `code.ips` + `exheader.bin` + `romfs/`, or an xdelta ROM patch | Fallback for OoT3D single screen; controller-neutral prompts |
| OoT3D: Single Screen Experience | OoT3D | GameBanana 695893 v3.2.2 | CC BY-NC-ND 4.0, no source | `exheader.bin` + `romfs/` (+ code) | Not used |
| SSE Controller UI Packs | OoT3D | GameBanana 701929 v3.2 | CC BY-NC-ND | textures | Not used: our own glyphs cover what The One Screen leaves (plans/xbox-glyphs.md) |
| Touchless Layout UI | OoT3D | GameBanana 567711 v3.1 | CC BY-NC-ND | textures | Fallback glyph pack (Vanilla build) |
| OoT3D Autosave | OoT3D | GameBanana 686090 | CC BY-NC-ND | `code.ips` only | Not used: our own autosave is in the patch (plans/oot3d-qol.md) |
| Door of Time Skip, Zora's River Waterfall Skip, Unfreeze Zora's Domain | OoT3D | GameBanana 713704, 711124, 710508 | CC BY-NC-ND | romfs only | Not used; Door of Time Skip can softlock adult Link |
| OoT3D Randomizer | OoT3D | github.com/gamestabled/OoT3D_Randomizer v5.0 | None for its own code: `LICENSE.md` only carries the MIT notice of the N64 OoT-Randomizer code it derives from | `code.ips` + `exheader.bin` | Evidence only: loader, Quick Text, the C-stick free camera (`code/src/camera.c`, by Nessy, HylianFreddy and Colin E, hooked at `Camera_Update`), story-flag skips at save creation |
| zeldaret/oot3d | OoT3D | WIP decompilation of the USA `code.bin` | CC0 | | Our fork is `decomp/oot3d` |
| DekUI / Majora ReMasked | MM3D | github.com/tgacquin/mm3d-dekui v1.1.0; github.com/OTPR26/DekUI-Fork v0.4.3 | GPL-2.0 | Azahar forks | Single screen is host code that needs the Qt frontend; the user declined Qt. Evidence for our own patch |
| Omni Xbox UI for PR | MM3D | GameBanana 569317 v1.3 (HardDanX) | CC BY-NC-ND 4.0 | textures | Not used: our own glyphs replace it (plans/xbox-glyphs.md) |
| MM3D Randomizer base patch | MM3D | Z3DR/mm3dr | MIT (with permission for its PR code) | | Reference |
| Zeldocto/MM3D-PC | MM3D | v0.1.9 | GPL-3.0 | native PC port | Not an Azahar mod |

No existing mod skips a cutscene on a button press. The randomizers set story flags at save creation;
Project Restoration skips only the Stone Tower statue cutscene.

## Our own patches

All MIT, written from scratch on each game's decomp (`decomp/oot3d`, our fork of zeldaret/oot3d;
`decomp/mm3d`, the player's mm3d-decomp), which holds the addresses they use and the RE behind them.
Each ships as the `code.ips` that rides on the set's baked `code.bin`.

- Press-Start cutscene skip, both games: [plans/cutscene-skip.md](plans/cutscene-skip.md).
- Quick Text and optional autosave for OoT3D: [plans/oot3d-qol.md](plans/oot3d-qol.md).
- Right-stick free camera: [plans/pad-features.md](plans/pad-features.md). Controls, item slots and the HUD: [plans/pc-controls-hud.md](plans/pc-controls-hud.md).
- Options: each patch lists its switches in `patch/<game>/options.toml` and keeps them, one byte
  each, in one block at the start of its payload (0x64C000 on the installed OoT3D set, 0x8F7000 on
  MM3D: the page after the base mod's `.bss`), holding the defaults. The app's per-game options
  menu writes that block (app.md, "Options"); nothing else in a patch decides whether a feature
  runs.
- Our own Xbox glyph textures for both games: [plans/xbox-glyphs.md](plans/xbox-glyphs.md).
- The games' own text from outline fonts, and its prompts' words for our inputs
  (`tools/z3dmods/text.py`, tables in `sets/text/`; [plans/pc-controls-hud.md](plans/pc-controls-hud.md),
  "Outline fonts"). The fonts are in `sets/fonts/` with their licences, both SIL OFL 1.1, which
  lets them be kept, bundled and redistributed with the tools as they are:

  | Game | Font | From | Why this one |
  |---|---|---|---|
  | OoT3D | Arimo (variable, weight 500) | github.com/google/fonts `ofl/arimo`, `Arimo-OFL.txt` | The game's font is a plain grotesque of Helvetica's shapes; Arimo has Helvetica's metrics and Latin-1 with Œ, œ, Ÿ and € |
  | MM3D | Jost (variable, weight 600) | github.com/google/fonts `ofl/jost`, `Jost-OFL.txt` | The game's font is a geometric sans in Futura's manner (round o, pointed W, two-storey a), semi-bold; Jost is that |
- MM3D single screen: [plans/mm3d-single-screen.md](plans/mm3d-single-screen.md).
- Fallback if The One Screen cannot be used: our own OoT3D single screen in the same patch, built from
  the Randomizer's loader and the CC0 decompilation (same plan file as MM3D's, OoT3D section).
- Optional romfs tweaks in the style of HugAMuffin's (Zora's Domain unfreeze, waterfall), opt-in,
  later; no Door of Time skip.
