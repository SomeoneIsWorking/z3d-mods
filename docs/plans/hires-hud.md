# High-resolution HUD textures (OoT3D first, MM3D after)

User: "The UI needs hires textures too..."; "you can also make your own one screen UI using the 4k
assets, no need to hand draw imo, ofc missing parts will be hand drawn, we have port-assets, you
can use and extend it".

Henriko's 4K pack replaces the base game's UI textures by hash, but The One Screen draws its HUD
with its own textures (the LB/RB/X/Y/A/B circles, the ITEMS/GEAR cross), which stay at its low
resolution.

- A tool composes a replacement for each low-resolution HUD texture at the pack's scale: the 4K
  pack's own art for what the base game has (hearts, rupee, item frames, panels) and port-assets'
  SVGs for button glyphs (`sets/gamepad-xbox360`). Only what neither has is drawn new, as SVG in
  port-assets, so other ports get it too.
- The output is keyed by Azahar's `tex1_<w>x<h>_<hash>` names, found with
  `z3dmods run oot3d <name> --harness --dump-textures`, and installed as its own pack beside the 4K
  pack. The One Screen's files are never edited.
- Henriko's pack states no licence, so the composed textures are generated into the player's
  install by the tool and never committed; the tool and the SVGs are ours (MIT).
- Our Xbox glyphs ([xbox-glyphs.md](xbox-glyphs.md)) come out of the same tool.

## Found so far
Link's house with the 4K pack on (`--dump-textures`, 2026-11-13): 53 textures have no replacement;
the HUD ones are four atlases, all at 3DS resolution:
- `tex1_128x128_30F1BADDAE4D61A8_4`: The One Screen's buttons (LB, RB, X, Y, B), target brackets,
  item slot frames and small icons.
- `tex1_256x256_F39D3FF515D11EDB_4`: "GAME OVER" and the action words (Attack, Check, Return,
  Open, Speak, ...) as bitmap text.
- `tex1_512x256_82FBA6C6E15F75AC_4`, `tex1_512x256_EE0FE761088F438D_4`: UI sheets (pills,
  panels, button art).
- Names such as "Link" and "File 1" are text the game renders into textures at run time; a texture
  pack cannot cover them.
The rest are scene textures the pack leaves alone (water, sky, glows).

## Built (2026-11-13)
- Azahar names pack files by CityHash64 over the decoded texture, rows bottom-up
  (`pica_texture.py`); reproduced exactly for RGBA4444, ETC1 and ETC1A4, so the pack's copy of any
  vanilla texture is found by name.
- The One Screen edits nine vanilla CTXB menu textures; each edited copy hashes differently, so
  the pack stopped covering it. `z3dmods install oot3d` (`"hud"` in `sets/oot3d.json`) builds a
  replacement for each from the pack's vanilla copy with the edits in
  `sets/hud/the-one-screen-891b60e.json` redone at the pack's scale (16x, 8x for name entry), and
  installs them under `load/textures/<title>/z3dmods-hud/`, named after The One Screen's textures.
  Re-lettered words and buttons keep the pack's vanilla lettering; moved art (swapped X/Y, the
  circled B) is copied from the pack; new labels (LB, RB, LT, RT, ITEMS, GEAR, digits, Navi, which
  the pack's copy lacks) are drawn in port-assets' key font; the A circle without its letter and
  the D-pad cross are rebuilt. An edit the table does not cover stops the install.
- Shown in game (`hud-hires`, on the Ally): file select, the Kakariko HUD and the item menu draw
  the 4K art with the labels crisp, where the One Screen's own textures were 3DS resolution.

## Still open
- MM3D: the same for Project Restoration's and DekUI's edits.
- The HUD's own labels are lettering; the text prompts' drawn Xbox glyphs are in [xbox-glyphs.md](xbox-glyphs.md).
