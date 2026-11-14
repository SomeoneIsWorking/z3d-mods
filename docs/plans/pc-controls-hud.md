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

## Status
Chord + ABXY: bindings built; the face buttons leaving the game is shown, an item on A or B is not. The rest not started.
