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

## Still open
- The app binds a pad by position, so these glyphs name the wrong button until it binds OoT3D by
  name (issues/pad-mapping-vs-hud-labels.md).
- MM3D: Project Restoration's HUD and prompts; the CC BY-NC-ND Omni pack stays installed until then.

## When
OoT3D first (built); MM3D follows. Until then the existing MM3D pack is installed as it is, for the player's private use (user: "I won't distribute them but I wouldn't mind having MIT variations").
