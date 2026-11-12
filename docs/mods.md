# Mods: inventory and the installed sets

What exists for OoT3D (USA, `0004000000033500`) and MM3D (USA, `0004000000125500`) on Azahar,
what we install and why. Checked 2026-11-12 against each project's source or release and against the
player's own dumps.

## The player's dumps

| Game | Product | Exheader remaster | `code.bin` sha256 (decompressed) | Revision |
|---|---|---|---|---|
| OoT3D | `CTR-P-AQEE` | v0 | `ef210566e1d9d16879a746dfb063fcbad232f0171d860de906531ecc526cc020` | USA Rev 1 executable: `.text` ends at 0x4C99A8, where ReFramed's and The One Screen's USA links place their loaders |
| MM3D | `CTR-P-AJRE` | v0 | `050a58834365187ca67211eef0a29816715e9ab9f7289e8372b06fb906620580` | USA 1.0: matches Project Restoration's v100 BPS source (size 0x5B1000, CRC32 0xC75021D9) |
| MM3D (Rev 1) | `CTR-P-AJRE` | v2 | `6eec27b1827eab2c87744ac38066d569ffe6fe75790b1eb3300bd91f5099038d` | USA 1.1: main partition (NCSD partition 0, 654643200 bytes) sha256 `9c756279…23da`, exactly DekUI's `supported-rom.json` |

No update title is installed for either game, and none of the chosen mods needs one. The MM3D
dump's NCCH has crypto flags set although its ExeFS and exheader are plaintext; `tools/z3dmods/ctr.py`
checks the plaintext ExeFS header instead of the flag.

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

`uv run --frozen z3dmods install oot3d mm3d` installs from `sets/<game>.json`. It checks the ROM's
`code.bin` hash, writes `z3dmods-install.json` beside the files it wrote, and on reinstall removes
only those (refusing a folder with files it did not write, or a file changed since).

### OoT3D: The One Screen

| | |
|---|---|
| Source | github.com/toadalle/The-One-Screen, commit `891b60e364cc660b2d1edfd7916ec01e3bb0cadf` (alpha 1.1.12); GameBanana 724248 |
| Licence | MIT for its own code and tools; game-derived assets and inherited headers excluded (`THIRD_PARTY_NOTICES.md`). GameBanana asks for permission before modifying: our changes stay in our own patch, not in its files |
| Gives | Single screen (HUD, items, gear, map, Ocarina, file select on the top screen), Xbox-style button labels, LB/RB items, X/Y slot swap, D-pad shortcuts (Up Items, Down Gear, Right Ocarina, hold Left View), Start pause, Select map |
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

### MM3D: Project Restoration v110 and Omni Xbox UI

| | |
|---|---|
| Source | github.com/leoetlino/project-restoration v1.5.9, `mm3d_project_restoration_v1.5.9-0-ge099fcc.7z`, sha256 `c575c1beeae8f7c68fc1126061b0f70d29cd9d0f5dd5697f53b83835638eca32` |
| Licence | GPL-2.0 |
| Gives | Zora fast swim, Deku hops, D-pad transformations, instant Bomber's Notebook, Song of Time saving option, boss rebalances, Inverted Song of Time at 1/3 speed, fewer song replays, Bombers no longer interrupting, softlock fixes |
| Layout | `v110/citra/code.bps` and `v110/citra/exheader.bin` for the player's Rev 1 (1.1) dump; the BPS source and target CRCs check against it. (`v100/citra/` was used on the 1.0 dump.) |

| | |
|---|---|
| Textures | Omni Xbox UI for PR 1.3 (HardDanX), GameBanana 569317, `omni_ui_for_project_restoration_13.rar` md5 `74d99a122706fbd46cec2252d88cac71` (matches GameBanana), sha256 `c512206ed5edbc00585b5477f588e63733b1deee167578f931bc2a50dcb20a21`; its `Omni Xbox UI for PR/` folder into `load/textures/0004000000125500/` |
| Licence | CC BY-NC-ND 4.0: installable, not modifiable or redistributable |
| Shown | Harness, new game to the Lost Woods, same state with textures on and off: the bottom screen's button labels become RB, a yellow Y, a blue X and LB+RB hints (stock: I, X, Y, II) |
| Needs | Azahar's custom textures, which the set turns on globally (`[Utility] custom_textures=true`; the only change to the player's `qt-config.ini`). Its readme also suggests swapping A/B and the bumpers with the triggers in Azahar's input mapping. That is the player's controller setup, so it is documented here and not changed |

MM3D single screen is not installed yet: every existing one (DekUI, Majora ReMasked) is a GPL-2 fork
of Azahar, not a mod for the player's Azahar 2126.2, and accepts only the USA 1.1 dump
(`supported-rom.json`: remaster v2, a fixed main-partition hash). The player's Rev 1 dump is that
dump, so DekUI takes it unchanged; the MM3D set moves to it (Project Restoration's v110 build). An MIT variant is planned in
[plans/mm3d-single-screen.md](plans/mm3d-single-screen.md).

## Everything else considered

| Mod | Game | Source, version | Licence | Layout | Status |
|---|---|---|---|---|---|
| Ocarina ReFramed | OoT3D | github.com/OTPR26/OOT3DHud v2.5.1 (2026-10-29), Single and Dual | GPL-3.0-or-later; free-cam base from Roberto-Nessy with no licence | `code.ips` + `exheader.bin` + `romfs/`, or an xdelta ROM patch | Fallback for OoT3D single screen; controller-neutral prompts |
| OoT3D: Single Screen Experience | OoT3D | GameBanana 695893 v3.2.2 | CC BY-NC-ND 4.0, no source | `exheader.bin` + `romfs/` (+ code) | Not used |
| Henriko MM3D 4K | MM3D | 3.0b-1, 1080p and 4K, henrikomagnifico.com/zelda-majoras-mask-3d-4k (Drive `1I_QuBQX-_TTKCkS0MHho73ApKFfyckh6`) | None stated | textures, `pack.json` | To install: Drive's download quota was exceeded on 2026-11-12 and the MediaFire mirror reset the connection. It remakes buttons and prompts, so where it and Omni replace the same texture Omni's file wins; if its `pack.json` sets `use_new_hash`, Omni's legacy-hash files would stop matching, so check before installing both |
| SSE Controller UI Packs | OoT3D | GameBanana 701929 v3.2 | CC BY-NC-ND | textures | To install for private use where The One Screen leaves 3DS glyphs |
| Touchless Layout UI | OoT3D | GameBanana 567711 v3.1 | CC BY-NC-ND | textures | Fallback glyph pack (Vanilla build) |
| OoT3D Autosave | OoT3D | GameBanana 686090 | CC BY-NC-ND | `code.ips` only | To bake into the base `code.bin` with The One Screen if their writes do not overlap; our MIT one later |
| Door of Time Skip, Zora's River Waterfall Skip, Unfreeze Zora's Domain | OoT3D | GameBanana 713704, 711124, 710508 | CC BY-NC-ND | romfs only | Not used; Door of Time Skip can softlock adult Link |
| OoT3D Randomizer | OoT3D | github.com/gamestabled/OoT3D_Randomizer v5.0 | MIT | `code.ips` + `exheader.bin` | Reference: loader, Quick Text, story-flag skips at save creation |
| zeldaret/oot3d | OoT3D | WIP decompilation of the USA `code.bin` | CC0 | | Reference |
| DekUI / Majora ReMasked | MM3D | github.com/tgacquin/mm3d-dekui v1.1.0; github.com/OTPR26/DekUI-Fork v0.4.3 | GPL-2.0 | Azahar forks | Accepts the player's Rev 1 dump; next for MM3D single screen |
| Omni Xbox UI | MM3D | as above | CC BY-NC-ND | textures | Installed until our own glyphs exist |
| MM3D Randomizer base patch | MM3D | Z3DR/mm3dr | MIT (with permission for its PR code) | | Reference |
| Zeldocto/MM3D-PC | MM3D | v0.1.9 | GPL-3.0 | native PC port | Not an Azahar mod |

No existing mod skips a cutscene on a button press. The randomizers set story flags at save creation;
Project Restoration skips only the Stone Tower statue cutscene.

## Our own patches

All MIT, written from scratch with the OoT3D Randomizer (MIT), zeldaret/oot3d (CC0) and the
player's own oot3d-decomp and mm3d-decomp as references. Each ships as the `code.ips` that rides on
the set's baked `code.bin`.

- Press-Start cutscene skip, both games: [plans/cutscene-skip.md](plans/cutscene-skip.md).
- Quick Text and optional autosave for OoT3D: [plans/oot3d-qol.md](plans/oot3d-qol.md).
- Our own Xbox glyph textures for both games: [plans/xbox-glyphs.md](plans/xbox-glyphs.md).
- MM3D single screen: [plans/mm3d-single-screen.md](plans/mm3d-single-screen.md).
- Fallback if The One Screen cannot be used: our own OoT3D single screen in the same patch, built from
  the Randomizer's loader and the CC0 decompilation (same plan file as MM3D's, OoT3D section).
- Optional romfs tweaks in the style of HugAMuffin's (Zora's Domain unfreeze, waterfall), opt-in,
  later; no Door of Time skip.
