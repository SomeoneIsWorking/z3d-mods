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

## Our own HUD
- One HUD per game drawn from the game's own state and art, with port-assets' Xbox glyphs on every
  button: item buttons, the chord page while the chord is held, hearts, magic, rupees, the
  minimap, the clock (MM3D). It replaces pasting the touch screen into the play view; full-screen
  menus may still show the touch screen.
- Each element sits where a console HUD puts it and occludes as little of the 3D view as possible.

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
Not started.
