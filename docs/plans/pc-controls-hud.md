# PC controls and our own HUD (OoT3D and MM3D)

User (2026-11-14): "Chord + ABXY gives you more item slots, that's the chord, we don't have to
stick to existing bindings for anything, I was also gonna ask you to make custom HUD with Xbox
glyphs and also keyboard HUD hotswap that can be like 12345678 for items, and mouse aim".

## Chord + ABXY
- Holding the chord turns A, B, X and Y into four more item buttons; released, they are the
  game's buttons again. Every binding is ours to choose: the 3DS layout and The One Screen's do
  not constrain it. The bindings live in one table in the app's pad owner, per game.
- The page mechanism stays (the save holds page one, pages per age/form, `pad-features.md`); what
  changes is which host buttons press the page's slots and what the HUD draws on them.

- Built in the app, 2026-11-14 (`app/src/pad_input.cpp`, `kChords`: one `ChordLayout` per
  title ID, bound when a game's options are applied). The chord is the left bumper in both
  games: the left index finger holds it while the left thumb steers and the right thumb
  presses a face button; the left stick's click, which it replaces, cannot be held while
  steering, and both triggers are taken (L targets, R shields). Held, it sets pad bit 12, on
  which the patches bring the second page onto the game's four item buttons, and the face
  buttons press those and nothing else: X is X, Y is Y, A is I, B is II. I and II are pressed
  the way each set presses them (OoT3D: ZL and ZR; MM3D: ZR with X and ZR with Y).
- The bumper's old job, ZL (OoT3D's item I, MM3D's View), is on the right stick's click. RB
  stays ZR.
- For the HUD: `PadInput::chord()` returns the bound game's `ChordLayout` (`pad_input.hpp`):
  the chord button and, per face button, the `ItemButton` it presses. The chord is held while
  `state().buttons[NativeButton::Debug]` is set. Until our HUD draws from it, the sets' HUD art
  is out of date: The One Screen's LB on item I and MM3D's LB badge on View name the old button.
- Shown with a real pad's events (`z3dmods inputtest chord`, OoT3D, File 1 beside Link's bed):
  LB held, the pad's A does not read the bed; released, it does. Not shown: an item used from
  A or B on the chord page (the test's file has no items), and MM3D.

## Our own HUD
- One HUD per game drawn from the game's own state and art, with port-assets' Xbox glyphs on every
  button: item buttons, the chord page while the chord is held, hearts, magic, rupees, the
  minimap, the clock (MM3D). It replaces pasting the touch screen into the play view; full-screen
  menus may still show the touch screen.
- Each element sits where a console HUD puts it and occludes as little of the 3D view as possible.
- Vector, drawn at the output resolution (user: "SVG HUD (item icons can stay as they are)"):
  button rings and frames, glyphs, key caps, hearts, magic and rupee frames, the clock and the
  minimap's frame are vector art (port-assets SVGs, extended there where missing) rasterized at
  the window's size, never a bitmap scaled up. The item icons stay the game's or the pack's
  raster art.

### Rule (user, 2026-11-14)
On the crop layout (`mm-ov2`, tabs and item buttons circled): "No man no, please do this properly
RE based instead of copying patches of bottom screen".
- No piece of the touch screen's framebuffer is drawn in the play view. The `play` layout's
  pieces in `patch/mm3d/overlay.toml`, and the mouse mapping for them, are deleted when the HUD
  below replaces them; there is no second implementation.
- The HUD is built from reverse-engineered state only: the item on each button and its icon, ammo,
  a button's enabled or dimmed state, hearts and their fill, magic, rupees and the wallet, the
  clock and day, the A button's action label, the minimap if shown. Each field and function is
  named in the game's decomp first (`decomp/mm3d` `docs/hud.md`, then `decomp/oot3d`), and the app
  reads the generated addresses through the core's guest memory.
- The game's own HUD pieces we replace are switched off at their owner by the patch, not masked.
- Positions are ours, a console layout; nothing has to sit where the 3DS put it.
- Stopgap: a menu (Items, Masks, Gear, the map, the notebook) still shows the game's touch screen
  whole. The proper fix is those menus drawn by us or moved by a patch; the risk is only looks.

### Design (one owner for both games)
- f93caea drew the touch screen's play elements as cropped pieces; the user rejected it (rule
  above). Its table of layouts stays for the menus; its `play` pieces go.
- `app/src/hud` is the one HUD owner. It knows element kinds, not games: a button (ring, glyph,
  item icon, count), hearts, a magic bar, a rupee counter, a clock, a minimap frame. A game is
  data: `patch/<game>/hud.toml` places elements on the top screen's 400x240 and names what each
  reads; the install resolves those names to guest addresses from the decomp and writes them
  into `z3d-options.json` as `"hud"`, the way `"screens"` is written.
- Art: port-assets SVGs (`gamepad-xbox360`, `keyboard`; hearts, frames, the clock and what else
  is missing are added there), installed beside the app with its fonts and rasterized at the
  window's size into one atlas (SDL3_image). The atlas is redrawn on a resize and on a device
  change; a frame only moves quads.
- The core draws it: one more `renderer_vulkan` commit next to `custom_bottom_pieces`, an RGBA
  atlas the frontend hands over and a list of quads (atlas rectangle, window rectangle, opacity)
  drawn after the screens. Tool screenshots go through the same pass, so a HUD frame is a proof.
- Item icons stay raster: the install copies each item's icon (the pack's copy where it has one)
  into the app's data by item ID; the HUD draws the icon of the item the game has on a button.
- Buttons: the HUD asks `PadInput::chord()` for the bound game's `ChordLayout` and
  `PadState::buttons[Debug]` for whether the chord is held, and draws that page's four items on
  A, B, X and Y. The table stays where the other agent put it (`kChords`, `pad_input.cpp`); the
  keyboard's 1 to 8 go into the same file's key table, each pressing a `ChordSlot`'s buttons.
- Last device: `PadInput` records whether the last press came from a pad or the keyboard; the HUD
  reads it every frame and swaps the glyph set.
- The game's own HUD has to go where ours replaces it (MM3D's A button and clock on the top
  screen, all of OoT3D's with The One Screen): a patch switch per element, found per game.

Order: the core's atlas pass; hearts, rupees and the item buttons for MM3D; the same table for
OoT3D; the chord page; key caps; the clock and minimap; hiding the games' own.

### Open
- Which guest state each element reads (button items, counts, the minimap's ownership) and the
  item icons' files: per game, into the decomps first.
- OoT3D runs The One Screen, whose HUD is drawn by the game on the top screen: ours replaces it
  only once a patch can switch its elements off.

## Outline fonts
User: "And SVG fonts" (an outline font, crisp at any resolution). Every piece of text the HUD
draws, and the game's own text (dialog, menus, item names), renders from an outline font
(TTF/OTF, SDF or MSDF) at the output resolution instead of the game's bitmap font scaled up. The
font is permissively licensed and close to the game's look; first check whether the game's text
sheets can be regenerated at the pack's scale from the outline font (texture replacement), and
draw in the app only what that cannot reach.

## Keyboard and mouse
- The HUD follows the last input device: a pad shows Xbox glyphs, the keyboard shows key caps.
  The switch is immediate (hotswap), with no restart.
- Keyboard items: 1 to 8 use the eight item slots directly (page one 1 to 4, page two 5 to 8), no
  chord needed.
- Mouse aim: in first person and aimed items (slingshot, bow, hookshot, boomerang), mouse motion
  turns the aim. Both games already aim with the gyroscope, so the app feeds mouse motion to the
  3DS gyro input first; patch code only if that cannot give 1:1 aim. Outside aiming, the mouse may
  drive the free camera.
- Built so far: the pad owner feeds the core a gyroscope (`PadState::gyro`, the `gyro:` step),
  and the core's HID reads it about 100 times a second with the value set (`mm-gyro4`). The mouse
  is not wired to it yet, because the game does not answer: in MM3D's first-person view, with
  Motion Controls on (`mm-opt3`), 40 degrees a second for half a second on each axis and
  direction leaves the view where it was (`mm-gyro3`). OoT3D's first-person view was not reached
  (`mm-ootgyro1`: D-pad left is the overhead view). Next: decompile what the first-person camera
  reads of the gyroscope in each game (OoT3D has `CalculateGyroscopeAxisStatus`) and find what
  it waits for: a steady rate may be taken for drift, or the accelerometer may have to agree.

## Status
- Chord + ABXY: bindings built, not yet seen in a run with a real pad.
- HUD (MM3D): the owner, the atlas and the core's pass are built (docs/app.md, "HUD"). Drawn from
  state: hearts, rupees, the X, Y, I and II buttons with their prompts, the chord page's prompts,
  key caps (`mm-hud2`, `mm-hud3`). The cut-out touch-screen pieces are gone from play.
- Missing from MM3D's HUD: the items' icons and ammo, a button's dimmed state, magic, the wallet,
  the A button's label and View; the game's own A ring and clock still draw (the clock is Project
  Restoration's and may stay). Each needs its field named in the decomp first (`docs/hud.md`).
- OoT3D: nothing drawn yet; its inventory is being written into `decomp/oot3d`.
- Keyboard: 1 to 8 and the last-device swap are built; seen only through the tools' `device`
  command, not with real keys in a window.
- Mouse aim: the gyroscope feed exists and the games do not answer it yet (below).
- Outline fonts: the HUD's own lettering is port-assets' outline typeface at the window's size;
  the games' text is not started.
