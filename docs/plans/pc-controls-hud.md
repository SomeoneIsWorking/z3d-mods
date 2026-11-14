# PC controls and our own HUD (OoT3D and MM3D)

User (2026-11-14): "Chord + ABXY gives you more item slots, that's the chord, we don't have to
stick to existing bindings for anything, I was also gonna ask you to make custom HUD with Xbox
glyphs and also keyboard HUD hotswap that can be like 12345678 for items, and mouse aim".

## Layout (decided 2026-11-14)
User: "think what a PC game with Xbox controller support would do", then "just make
something". Fable's design (modern verbs on fixed buttons, D-pad slots) plus the user's LB chord,
whose objection (a hidden layer) our HUD answers by drawing the chord page while LB is held.
Same layout in both games. Built: `app/src/pad_input.cpp` holds every table, and `z3d --keys`
prints the keyboard's and the mouse's.

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
| D-pad | Item slots 4 to 7 (Up, Right, Down, Left); a new file has the ocarina on Down |
| LB held | Chord: A, B, X, Y are item slots 8 to 11; the HUD shows them while held |
| Menu | Item screen; a slot's input there assigns the cursor's item to it, Menu again leaves |
| LB and Menu | Pause (MM3D: the Gear screen, where its options are) |
| View | Map |

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

Menu is the item screen and not Pause as first decided: the game's Start in play is only OoT3D's
"Save your progress?" and does nothing visible in MM3D (`ctl-pad1`), while the item screen is
where slots are set. Outside play (menus, the ocarina, a text box) every host button is the 3DS
button of its name, the triggers L and R, the bumpers ZL and ZR.

Songs on slots (user: "Imo songs can be added to item slots"). Built, both games.
- A learned song is assigned like an item: with the ocarina out, RB opens the song list, and a
  slot's input there takes the song on show.
- Pressing the slot plays the song through the game's own path: the ocarina comes out and the
  notes are played as the game plays them, so the game's own effects and prompts happen. MM3D's
  Song of Time ends in the game's own "Do you want to save?" (`ctl-pad2`, the same by hand in
  `ctl-mm9`); the game asks nothing before the return, and nothing is skipped.
- So MM3D's Song of Time is the one slot that must be held: half a second (`kHoldUpdates` in
  `patch/mm3d/slot_items.cpp`), the slot's ring filling in gold on the HUD meanwhile; a tap or a
  shorter hold does nothing (`ctl-hold3`). Every other song plays on the press.
- The HUD shows a note on the slot.

Not done on purpose: no item on a trigger or on R3, no radial wheel, nothing equips boots or
masks by context.

## Our own HUD
- One HUD per game drawn from the game's own state and art, with port-assets' Xbox glyphs on every
  button: the item slots, the chord's four while the chord is held, hearts, magic, rupees, the
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

### The buttons on the HUD
- Top right, the same in both games: the pad's diamond (A with the action's word, X the sword, Y
  and B their slots), RB's slot beside it and the D-pad's cross with its four slots. While LB is
  held the diamond shows slots 8 to 11. A slot with a song shows a note. With the keyboard last
  used every ring carries its key's cap, the sword and shield the mouse button's.
- User: "Just show A B X Y only always and when cord is held it shows the alt versions", then the
  layout above added RB and the D-pad.
- Counts sit on a dark plate that hangs below the ring, off the icon ("numbers overlap the icons
  too much"; a bare count on the rim read "30" as "3.0").
- The rupee, the key and the hearts are the game's own art, the texture pack's copy ("use the 4k
  ones"); frames, the meter and glyphs stay vector.

### Our own item slots (user, 2026-11-14)
"we don't have to stick to what 3D games do, we can move away from this I and II things and make
it so we can dictate inputs". Also: "let's not bind an item to right stick click."

- Eleven slots, ours, the same in both games. The game's own item buttons are lanes the patch
  uses and the player never sees or binds.
- App to patch: one block of the patch (`tools/z3dmods/slot_link.py`, `"link"` in
  `z3d-options.json`; `app/src/slot_link`). The app writes the slots held this frame; the patch
  writes the pad table the game wants (by name, play, or a screen that assigns), and each slot's
  item, song and lane for the HUD.
- Patch (`patch/<game>/slot_items.cpp`, `patch/common/slots.hpp`): a held slot's item goes onto a
  free lane and that lane's button is pressed in the same update; the lane is given back when
  another slot needs it. OoT3D: an item in hand that loses its lane is put away by the game,
  which drops presses until Link's hands are empty, so the new slot's press waits for that
  (decomp/oot3d `docs/item_buttons.md`, "The item in hand").
- Store: a slot's inventory slot or song in the save's spare bytes (OoT3D
  `SaveContext.unused_6C`, MM3D the spare rows of the equipment's button slots). The lanes hold
  the game's own layout while an item screen is up and before a save, so the file loads the same
  without the patch.
- Assigning: in the item screen (MM3D: Items and Masks) a slot's input takes the cursor's item;
  in the ocarina's song list it takes the song on show. The left stick moves both cursors.

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
- Buttons: the HUD draws each slot from `SlotLink` and asks `PadInput::slotPrompt` and
  `verbPrompt` what presses it; `app/src/prompt_art` maps a prompt to its glyph or cap, for the
  HUD and the menus' hints alike.
- Last device: `PadInput` records whether the last press came from a pad or the keyboard; the HUD
  reads it every frame and swaps the glyph set.
- The game's own HUD has to go where ours replaces it (MM3D's A button and clock on the top
  screen, all of OoT3D's with The One Screen): a patch switch per element, found per game.

### Open
- Which guest state each element reads (button items, counts, the minimap's ownership) and the
  item icons' files: per game, into the decomps first.
- The A button's words are cut from the US English sheet (`[words]` in `hud.toml`); another
  language's file needs its sheet named by the file's language.
- The item screens are the game's art: their BACK and Return tabs carry a circled B, which in
  an item screen is slot 2's button; Menu leaves them.

### The game's own prompts
- A text box's button prompts are cells of the game's prompt sheet, redrawn with pad glyphs
  (`sets/hud/*.json`, plans/xbox-glyphs.md). The four item-button cells (X, Y, I, II) name where
  our slots start: Y, B, RB and the D-pad. OoT3D, the Know-It-All Brothers (`ctl-text5`): "The
  RB, Y, B, and D-pad icons on the lower screen are your item slots", "Assign items you use often
  to Y or B". MM3D's item messages name the X and Y item buttons with the plain X and Y cells
  and only I and II with their own: the Hero's Bow message (`ctl-text7`, a poster in South Clock
  Town given its text) reads "Set it to X, Y, RB or D-pad on the Select Item Screen". Y, RB and
  the D-pad are slots; X is the sword.
- What a cell cannot say is said in the message data: the install rewords the prompts
  (`sets/text/`, "The games' text" below), so the sword's prompt is X and the item messages
  name the slots.
- No key caps in text boxes: the sheet is a texture made at install, not swapped by device.
- MM3D's touch-screen HUD sheet still labels I, II, View, Gear and Map with Project
  Restoration's shortcuts; that sheet is not drawn in play any more.

## Outline fonts
User: "And SVG fonts" (an outline font, crisp at any resolution). Every piece of text the HUD
draws, and the game's own text (dialog, menus, item names), renders from an outline font
(TTF/OTF, SDF or MSDF) at the output resolution instead of the game's bitmap font scaled up. The
font is permissively licensed and close to the game's look; first check whether the game's text
sheets can be regenerated at the pack's scale from the outline font (texture replacement), and
draw in the app only what that cannot reach.

### The games' text (built 2026-11-14)
The path is texture replacement, in both games, with nothing drawn by the app:
- MM3D hands each sheet of `message/ltn16.gzf` to the GPU whole, a 256x256 A4 texture named by
  the hash of its bytes (decomp/mm3d `docs/formats/gzf.md`). The install writes the font file
  with its glyphs drawn from the outline font and the pack's image of each sheet at 8x.
- OoT3D copies a glyph's cell into a texture as text first uses it, so every message makes
  another texture and none can be keyed. Its text renderer has a second mode no caller uses,
  one texture of the font's characters by code, filled once; our patch turns it on for every
  renderer and gives the characters past 255 (the dash, the quotes, Œ) the control codes'
  cells (`patch/oot3d/font_atlas.cpp`, decomp/oot3d `docs/message.md`, "Drawing glyphs"). The
  texture is then a function of the font file, and the install writes both as for MM3D.
- Why not text drawn by the app: the game keeps its own layout, clipping, colours, fades and
  order under its menus for free, in every screen that prints text, and no patch has to
  publish any of it. What it costs: glyphs sit on the game's whole pixels (each glyph is
  stretched to a whole advance, under 6%, so lines keep an even rhythm), the images are 8x, so
  past 8x they are scaled up, and below 4x the core samples them without mipmaps: at 1x the
  letters are legible but rough. Without the pack's folder the game prints the same font from
  its file at 1x.
- `tools/z3dmods/text.py` owns it; a game's table is `sets/text/<game>.json` and the fonts are
  in `sets/fonts/` (docs/mods.md has the licences and the choice). The outline font is fitted
  to the game's own font: capitals as tall and on the same row, tracked until a few lines of
  prose are as long (OoT3D: Arimo at 13.08 px an em; MM3D: Jost at 14.29).
- Prompt words: the table's `edits`, regular expressions over a message's markup, English
  only. OoT3D, 58 messages: the sword's B is X except in the ocarina's messages; the item
  buttons are in the slots' order (Y, B, RB, D-pad); "Tap the Items icon on the lower screen"
  is "Press Menu" (the START icon, drawn as Menu); the map's "Tap ... on the lower screen" is
  "Press View"; the Know-It-All Brother's item lesson (0x1036) is written again for slots and
  names the LB chord. MM3D, 78 messages: the sword's and the forms' B is X except where the
  pad is by name (the ocarina, a cancel, a minigame's Quit); "Set it to X, Y, I or II on the
  Select Item Screen" is "Set it to Y, B, RB or D-pad on the Items screen".
- Seen (harness, with the pack): OoT3D's Know-It-All Brother at 1x and 4x (`txt-oot-r1`,
  `txt-oot-r4`): "Y, B, RB, and each direction of D-pad are your item slots", "first press
  Menu to open the Items subscreen", "Hold LB, and A, B, X, and Y are four more slots", in
  Arimo with clean edges at 4x. MM3D's Hero's Bow message on the South Clock Town poster
  (`txt-mm-r1`, `txt-mm-r4`): "Set it to Y, B, RB or D-pad on the Items screen" and "You can
  also use LT to automatically aim at a target", in Jost.
- Not done: the prompts do not follow the last device. The prompt sheet is one texture made
  at install; following the device needs the core to swap that one replacement while the game
  runs (a second image of the sheet with key caps, and a setting the app flips), which is not
  built. French and Spanish are not reworded. OoT3D's Gear, Navi, View and Ocarina touch icons
  ("Tap ... to open the Gear subscreen", 0x1039's map display on the +Control Pad) still name
  the touch screen: which input opens each in our layout has to be seen first. MM3D's
  remaining "Tap ... on the lower screen" messages (six) likewise. The fonts' characters the
  outline font lacks keep the game's bitmap, enlarged (MM3D's Greek and Cyrillic sheets, the
  US text uses none). sys8.qbf is not redrawn.

## Keyboard and mouse
- The HUD follows the last input device: a pad shows Xbox glyphs, the keyboard shows key caps.
  The switch is immediate (hotswap), with no restart.
- The menus' hints (picker, options, folder browser) follow it the same way
  (`MenuCanvas::hints`).
- The tools read the keys from the app (`z3d --keys`, `tools/z3dmods/app_keys.py`), never a copy.
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
- Slots, pad, keyboard and HUD: built in both games. Seen with a real pad's and real key events
  in a window at 1280x720 and 854x480 (`z3dmods inputtest slots`, `slots-small`, `slots-mm3d`,
  `slots-mm3d-small`; files made on the harness by the game's own save): Menu opens the item
  screen; Y, RB, D-Right and LB+A take the cursor's items and the panel shows them; each is used
  in play from its slot and the count falls; LB held shows slots 8 to 11; key 1 uses slot 1 with
  caps on the HUD; D-Down brings the ocarina, RB its song list, D-Up takes the song; from play
  D-Up plays it (OoT3D: "You played the Sun's Song"; MM3D: the Song of Time, the return to the
  first day).
- Saves: a file saved with slots set loads without the patch with the game's own buttons
  (OoT3D `ctl-save3`; MM3D `ctl-mmsave2`, `ctl-mmload`).
- Menu hints: `inputtest picker`, `options`, `options-oot3d` show pad glyphs, then caps after a
  key, and back.
- MM3D's map, ocarina and Gear verbs press Project Restoration's own shortcuts (ZR with
  Select, A and Start; `kGames` in `pad_input.cpp`), seen with real keys and a pad (`inputtest
  verbs-mm3d`).
- MM3D's Masks screen with a pad (`inputtest masks-mm3d`): a click on the Masks tab, the stick
  to the Bunny Hood, D-Left takes it; in play D-Left puts it on and takes it off; R3 enters
  first person and leaves it. `slots-mm3d` uses four slots in a row, which fills the game's
  four item buttons (I, II, X, Y), so the X and Y lanes run there.
- Not built: the mouse as the free camera outside aim. The right stick is the C-stick, which
  OoT3D's `free_camera.cpp` and Project Restoration read as a rate; a mouse wants an angle, so
  the next step is a yaw and pitch delta in the link block that `free_camera.cpp` adds to its
  orbit, and for MM3D the RE of Project Restoration's camera input.
- Not done: item counts are not coloured at capacity.
- Missing from MM3D's HUD: View, the minimap (what decides it is not found), the ice and light
  arrows' and the powder keg's counts; the third wallet's red gem and 500 were not run. The
  game's own clock still draws and is not ours yet.
- Missing from OoT3D's HUD: the carrots and View; the bow's, bombchus' and beans' counts follow
  the N64's slots and were not seen.
- Mouse aim: built and measured in both games, in first person and with the bow (MM3D) and the
  Slingshot (OoT3D): a mouse delta turns the view by 0.05 degrees a count within 0.03 degrees
  (`aim-mm5`, `aim-oot3`), and in a window the F key, real mouse motion and leaving the look
  (`inputtest aim`, `aim-mm3d`). Not run: the hookshot, boomerang and OoT3D's bow; Master Quest.
- Outline fonts: the HUD's own lettering is port-assets' outline typeface at the window's size;
  the games' text is drawn from Arimo and Jost through the texture pack ("The games' text").
