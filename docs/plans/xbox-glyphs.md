# Our own Xbox button glyph textures (OoT3D and MM3D)

User, on the CC BY-NC-ND glyph packs: "I'd rather make MIT versions of those then".

- Our own drawn textures replace the HUD and prompt button textures: generic A/B/X/Y in the familiar
  green/red/blue/yellow positions, plus bumper and trigger shapes. They are not copied from Microsoft
  assets or from HardDanX's or chubbycubby's art.
- OoT3D: The One Screen already draws Xbox labels on its HUD; first list which prompts it leaves
  as 3DS glyphs and cover only those.
- MM3D: cover Project Restoration's HUD and prompts, as Omni UI does, keyed by Azahar's
  `tex1_<w>x<h>_<hash>` names, which we find by dumping the textures ourselves.
- Once ours exists, the CC BY-NC-ND Omni pack leaves the installed set.

## Built (OoT3D, 2026-11-14)
- What The One Screen leaves as 3DS art is the text prompt sheet, `message/parts.ctxb`: the circle
  pad and D-pad with each direction, A/B/X/Y, L, R, START, the I and II item buttons (lit and dim),
  the X and Y item buttons (lit and dim) and the Ocarina note row. Its HUD and menus already carry
  Xbox labels (hires-hud.md); "L-TARGETING" in the options screen and the small circled B on the
  BACK tabs stay.
- `sets/hud/the-one-screen-891b60e.json` lists each of those cells as a `glyph` edit and
  `tools/z3dmods/hud.py` draws port-assets' `sets/gamepad-xbox360` SVG there at the pack's scale,
  for the button The One Screen binds: circle pad → left stick, L/R → LT/RT, I/II → LB/RB, START →
  the menu button, A/B/X/Y in their Xbox colours. port-assets gained the two-way stick and D-pad
  glyphs for it.
- Shown in game (`oot-ch6`, the Know-It-All Brothers' house, `warp:0xC9`): "The LB, X, Y and RB
  icons on the lower screen are your item slots" draws the LB and RB pills, the blue X and the
  yellow Y.
- Not seen in a dialog yet: the stick, D-pad, A, B, LT, RT and menu glyphs (same sheet, same edit).

## Built (MM3D, 2026-11-14)
- MM3D's menu art is in GAR archives under `layout/` (`tools/z3dmods/gar.py`). The 3DS prompts it
  draws: the text prompt sheet `Joker.Main.gar/parts.ctxb` (A/B/X/Y, L, R, START, D-pad and
  circle pad with each direction, the X and Y item buttons, ZL, ZR), the A and B letters and the
  X and Y slot labels on the HUD sheet, the circled B on Return, Cancel and No, the circled A and X
  on the map, world map and Bombers' Notebook buttons, and the note letters on the Ocarina screen.
- `sets/hud/mm3d-usa-rev1.json` lists those cells; `"hud"` in `sets/mm3d.json` builds them on
  Henriko's 4K pack's copies (8x, 16x for the Return button) into
  `load/textures/<title>/z3dmods-hud/`. Where the pack draws its own button on a face (Return,
  Cancel, No, Confirm, the notebook's Set, Remove and Skip) the glyph is a `badge` placed to cover
  it, so some boxes sit on fractions of a game pixel; the HUD's A and B, which sit on the ring,
  are `inlay`s: the art is carried across under the glyph.
- The Omni pack left the set; with it went the app's X/Y trade, since ours name X and Y as the pad
  does.
- Shown in game (`mm-gl2`, South Clock Town): the green A on the action button, the blue X and
  yellow Y on the item slots, the red B on the Gear and Map screens' Return.

- Pad hints for the touch-only buttons (2026-11-14): Project Restoration puts item I on ZR with X
  and item II on ZR with Y, and ZL opens View. The HUD sheet's I and II labels and the text
  sheet's I and II cells are drawn as the bumper over the face button (`chord` in the table), and
  View carries an LB badge (`badge`).
- Shown in game: a dialog (`mm-gl4`, the owl statue's message set to the Hero's Bow's: "Set it to
  X, Y, RB over X or RB over Y", and the Mirror Shield's "Press RT" in `mm-texts`), the HUD's LB
  on View and RB hints on I and II (`mm-gl4`, `mm-pad2`), the Ocarina screen's LT, RT, X, Y, A and
  the B on Cancel (`mm-oc3`), the Bombers' Notebook's B on Return (`mm-pad2`). ZR with X and ZR
  with Y play the ocarina from I and from II, ZL looks around (`mm-pad1`, `mm-pad2`).

- On the pack (2026-11-14, `mm-hires`): the HUD, the Ocarina screen, Items and the map draw the
  pack's art with our glyphs; `textures:off` in the same run shows the game's I, II, X, Y and
  yellow A again. A dialog's prompts (`mm-hires-text`, the Hero's Bow message) are the pack's sheet
  with ours on it.

## Still open
- MM3D, not seen in game yet: the world map's zoom prompts, a notebook entry's A and X buttons
  (the notebook was empty).
- MM3D: only the English sheets are covered; French and Spanish keep the 3DS prompts.
- MM3D: Project Restoration's other shortcuts (Start the notebook, Select Items, ZR with Start
  Gear, ZR with Select the map, ZR with A the ocarina; all seen working in `mm-pad1`) have no hint
  on the buttons they open.
- The Gossip Stone model's button texture (not in a layout archive) is not looked at.
