# PC controls and our own HUD (OoT3D and MM3D)

User (2026-11-14): "Chord + ABXY gives you more item slots, that's the chord, we don't have to
stick to existing bindings for anything, I was also gonna ask you to make custom HUD with Xbox
glyphs and also keyboard HUD hotswap that can be like 12345678 for items, and mouse aim".

## Layout (decided 2026-11-14)
User: "think what a PC game with Xbox controller support would do", then "just make
something". Fable's design (modern verbs on fixed buttons, D-pad slots) plus the user's LB chord,
whose objection (a hidden layer) our HUD answers by drawing the chord page while LB is held.
Same layout in both games; this supersedes the bindings in the sections below.

| Pad | Job |
|---|---|
| Left stick | Move |
| Right stick | Free camera; aim in first person and aimed items (the mouse too) |
| R3 | First person; again to leave |
| L3 | Nothing |
| A | Context action (talk, open, climb, roll, put away) |
| X | Sword (hold for spin); MM form action |
| Y, B, RB | Item slots 1 to 3 (RB keeps the right thumb on aim) |
| RT | Shield; fires the item in hand while aiming |
| LT | Hold to Z-target; tap to recenter |
| D-pad | Item slots 4 to 7; defaults Down ocarina, MM Left/Up/Right Deku/Goron/Zora |
| LB held | Chord: A, B, X, Y are item slots 8 to 11; the HUD shows them while held |
| Menu | Pause; in the item screen a slot's input assigns the cursor's item to it |
| View | Map; MM: a second press opens the Bombers' Notebook |

| Keyboard and mouse | Job |
|---|---|
| WASD | Move |
| Mouse | Camera and aim |
| E or Space | Context action |
| Left mouse | Sword |
| Right mouse | Shield |
| Q or middle mouse | Target / recenter |
| F | First person |
| 1 to 8, then Shift+1 to 3 | Item slots 1 to 11 |
| R | Ocarina |
| Tab | Item screen; M map; Esc pause |

Not done on purpose: no hotkey for a song (a stray Song of Time wipes a cycle), no item on a
trigger or on R3, no radial wheel, nothing equips boots or masks by context.

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
- The pad's X and Y are the X and Y item buttons on both pages, pressed as the bound game's
  layout says. The One Screen trades the pad word's X and Y bits in play (its
  `source/input/policy.h`; on the harness the 3DS X reads 0x800 in `GameState.buttonsHeld`,
  `oot-ch15`), so for OoT3D the pad's X holds the 3DS Y and the pad's Y the 3DS X. MM3D reads
  them straight (0x400 for X, `mm-xy1`).
- Shown with a real pad's events, in a file the test makes on the harness with the game's own
  save (items poked into the inventory and named on the second page):
  - OoT3D (`z3dmods inputtest chord`, File 1 saved inside the Deku Tree, a Slingshot on X and a
    Deku Stick on II of the second page): LB held, the HUD draws the Slingshot on X and the
    stick on B's slot; LB and X held, Link aims the Slingshot in first person; LB and B, Link
    has the stick in hand. Before the pad's X held the 3DS Y for this game, LB and X did nothing.
  - MM3D (`z3dmods inputtest chord-mm3d`, File 1 saved at South Clock Town's owl statue, bombs
    on X and a bow on II of the second page): LB held, the HUD draws both; LB and B held, Link
    aims the bow; LB and X, he lifts a bomb.
  - A on the chord page (item I) aimed the Slingshot and Y the bow in the test's earlier files.
  - The Slingshot is not usable in Link's house, where a child's file saved outdoors loads.

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

### The button cluster (user, 2026-11-14)
"Just show A B X Y only always and when cord is held it shows the alt versions."
- The cluster is four buttons, A, B, X and Y, in the pad's diamond, top right, the same in both
  games. A button's glyph never changes; only what it shows does.
- Released: A is the action with its word, B the sword, X and Y their items.
- Chord held: each shows what its press does then. A shows the I button's item and B the II
  button's (the patch has traded the second page onto the game's four item buttons), X and Y the
  second page's X and Y items.
- Not on the HUD: the first page's I and II items. The bindings that press them stay as they are
  (`kChords`, each set's own I and II buttons).
- Counts sit on a dark plate: the ring's rim behind a bare count read as a decimal point ("30"
  as "3.0"). The plate hangs below the ring, off the icon ("numbers overlap the icons too much"
  about the plate in the ring's corner).
- The rupee, the key and the hearts are the game's own art, the texture pack's copy ("use the 4k
  ones"); frames, the meter and glyphs stay vector.

### Our own item slots (user, 2026-11-14)
"we don't have to stick to what 3D games do, we can move away from this I and II things and make
it so we can dictate inputs". Also: "let's not bind an item to right stick click. We have a ton of
slots anyway like d-pad and chord dpad are available as well."

This replaces the two pages on the game's Y, X, I and II buttons. Not built yet; until it is, the
chord page above is what runs.

- The player has N item slots (at least 8), ours, the same in both games. The game's Y, X, I and II
  buttons become lanes the patch uses and the player never sees or binds.
- Bindings: one table per game in the pad owner (`app/src/pad_input.cpp`, where `kChords` is now):
  each slot's host inputs, any of a face button, chord and a face button, a D-pad direction, chord
  and a D-pad direction, a key (1 to 8 and on). No slot is on the right stick's click. A set's
  own job on an input a slot takes (The One Screen's and Project Restoration's D-pad shortcuts)
  moves to a free input in the same table, and each move is listed here when it is made.
- App to patch: the pad owner writes the slots held this frame, one bit a slot, into a block of the
  patch that the app already writes (the options block's neighbour, its address in
  `z3d-options.json`). The pad word has one spare bit (12), too few for a slot number.
- Patch: on a held slot it puts the slot's item on a free lane (the game's X or Y button, so two
  slots can be held at once), sets that lane's button in the play input itself, and gives the lane
  back when the slot is released: the trade `chord_items.cpp` does today, per slot instead of per
  page. The press and the trade are one step in the patch, so no frame separates them.
- Where a slot's item lives: an inventory slot number per item slot in the save's unused bytes
  (OoT3D `SaveContext.unused_6C`, MM3D the unused rows of `EquipmentData.buttonSlots`), as the
  second page is kept now. The game's own button layout in the file is never ours: before a save
  the lanes hold what the game put there, so the file loads the same without the patch.
- Assigning: in the item screen the player presses a slot's input on the item under the cursor.
  The patch reads the screen's cursor and writes the slot. What the screen's cursor is (the
  selected node, in both games) is not reverse-engineered yet: that is the first RE step, into the
  decomps.
- HUD: A, B, X and Y as decided above, drawn from the slot table: each button shows the slot its
  press uses now, released and with the chord. Slots on other inputs (the D-pad) are not drawn.
- Order: the cursor's RE; OoT3D; MM3D. Proof: items used from D-pad and chord and D-pad slots with
  real pad events, and a file saved with slots set loading clean without the patch.

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
- OoT3D's words for the A button are English in `hud.toml`; the game's other languages need
  their own lists, or the words cut from the game's `hud_all00.ctxb`.

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
- What the games read (decomp `docs/motion.md`, both games, the same code): the first-person look
  adds the SDK gyroscope reader's direction (the rate integrated) to the look base, times 2.1 in
  yaw and 1.46 in pitch.
- Built, 2026-11-14 (`app/src/mouse_aim`, docs/app.md "Mouse aim"): the mouse is captured while
  the view is first person and follows the gyroscope, and released when it ends; a count is
  0.05 degrees of view on both axes. The app's motion device gives the core every HID sample
  itself (no frame-time scaling), the reader's dead zone, zero drift, averaging and
  accelerometer revise are off for it with the bias 0, pitch turns the 3DS around its x and yaw
  around world up, and the look's limits do not wind up. No patch code was needed.
- Not built: the free camera on the mouse outside aiming, a sensitivity option, firing from a
  mouse button.

## Status
- Chord + ABXY: bindings built, not yet seen in a run with a real pad.
- HUD, both games (docs/app.md, "HUD"): the owner, the atlas and the core's pass are built. Drawn
  from state: hearts by quarters, magic, rupees (the wallet's gem in MM3D, green at the wallet's
  capacity), the A, B, X and Y diamond with item icons and counts on plates, the chord's alt
  contents on the same four buttons, key caps (`hud-ootlook3`, `hud-ootlook5`, `hud-mmlook3`,
  `hud-mmlook5`). OoT3D also: small keys in the dungeons and the action's word under A. The rupee,
  key and hearts are the pack's copies of the game's art. The cut-out touch-screen pieces are
  gone from play, and The One Screen's own HUD is switched off in the patch (`base_hud`).
- Missing from both: our own item slots (above); an item equipped through the item screen, a
  count changing in play and a scene change were not seen, only values written into memory; the
  first page's I and II items are not drawn; item counts are not coloured at capacity.
- Missing from MM3D's HUD: the A button's word (the number is found, its mapping is not), View,
  the minimap (what decides it is not found), the ice and light arrows' and the powder keg's
  counts; the third wallet's red gem and 500 were not run. The game's own A ring and clock still
  draw: hiding the A button's widget is found, the clock alone is not, and no code patch has been
  built yet.
- Missing from OoT3D's HUD: the carrots, the D-pad cross and View; the bow's, bombchus' and beans'
  counts and the action words past "Check" follow the N64's numbering and were not seen.
- Keyboard: 1 to 8 and the last-device swap are built; seen only through the tools' `device`
  command, not with real keys in a window.
- Mouse aim: built and measured in both games, in first person and with the bow (MM3D) and the
  Slingshot (OoT3D): a mouse delta turns the view by 0.05 degrees a count within 0.03 degrees
  (`aim-mm5`, `aim-oot3`), and real mouse motion in a window does the same (`inputtest aim`,
  `aim-mm3d`). Not run: the hookshot, boomerang and OoT3D's bow; Master Quest.
- Outline fonts: the HUD's own lettering is port-assets' outline typeface at the window's size;
  the games' text is not started.
