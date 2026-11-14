# Pad features: right-stick free camera and chord item buttons (OoT3D and MM3D)

User: "Right stick free cam + chord buttons for item uses (on top of Y and B) + Xbox glyphs +
whatever you can think of". Glyphs: [xbox-glyphs.md](xbox-glyphs.md). Each feature is our own
code in the MIT patch, with a toggle in the per-game options menu, on by default.

The app feeds the host's right stick to the 3DS C-stick and the triggers to ZL/ZR
(`app/src/pad_input.cpp`), so both features read the New 3DS inputs in the guest.

## Free camera
- OoT3D never reads the C-stick. The patch reads it from the `ir:rst` service's shared memory with
  its own client (`patch/common/ctr.hpp`, `cstick.hpp`, written from 3dbrew's ABI tables).
- The orbit is ours: yaw and pitch from the stick around the game's own look-at point, the radius
  the game's, the eye pulled in front of collision at once and let out again gradually. The
  game's camera still runs first, so its look-at point, quake and setting changes stay.
- It holds only while the game runs Link's follow camera (`Camera_Normal1`, or `Camera_Jump1` in
  a jump), read from the game's own settings table: Z-targeting, first person and aimed items,
  talking, climbing, hanging, the horse, crawlspaces, cutscenes, scene changes and fixed or
  scripted camera settings hand the view back. L recenters behind Link because Z-targeting does.
  Options: on/off, invert X, invert Y (`patch/oot3d/options.toml`); a Master Quest file's mirrored
  world turns the stick's x around.
- Hooks: both `bl Camera_Update` calls for the active camera (`Play_Update`, and `Gameplay_Draw`'s
  repeat). The One Screen ships an orbit camera of its own on a hook after `Play_Update`; our patch
  takes that hook back so only one camera places the eye (docs/mods.md).
- The OoT3D Randomizer's free camera (no licence for its own code), Ocarina ReFramed (GPL-3.0) and
  Roberto-Nessy's Free Cam (no licence) are evidence only. RE: decomp/oot3d `docs/camera.md`.
- MM3D already turns the camera with the C-stick on New 3DS; the patch adds the invert options
  and the recenter button there only if the game's own does not cover them.

### Status (MM3D, 2026-11-14)
The game's own C-stick camera works in the app (`mm-cstick`, South Clock Town: right turns the
view, up and down tilt it) and L recenters as on the console. Invert X and Y are app options that
turn the stick around before the game reads it (`patch/mm3d/options.toml`, `pad`; `mm-cstick-inv`:
both on, the same stick input turns and tilts the other way). No patch code.

### Status (OoT3D, 2026-11-14)
Built (`patch/oot3d/free_camera.cpp`). Seen on the harness, File 1 as a child:
- Kokiri Forest (`warp:0xEE`, runs `oot-fc6`, `oot-fc8`): the stick turns the view around Link to
  his front, tilts it up to 28 degrees from below and down to 67 from above, and keeps its angle
  while he walks and while he falls; the radius stays the game's (136.7).
- Hand-back on the same frame: L held (the view goes behind Link with the targeting bars), first
  person (D-pad Left), talking. The Market (`warp:0xB1`, a fixed setting) and the Deku Tree's
  opening cutscene (`warp:0x0`) do not react to the stick.
- Dodongo's Cavern's entrance corridor (`warp:0x4`): turning the view through the side walls
  keeps the eye inside the corridor.
- Options poked into the block: invert X and invert Y turn the other way, off leaves the game's
  camera alone.
- Not seen in a run: the horse and a crawlspace (their settings run other camera functions in
  the game's table, so the orbit cannot hold there), a Master Quest file, and the options menu.

## Chord item buttons
- While the chord button is held, the four item buttons (Y, X and the two the HUD draws as LB and
  RB) hold a second page of items; released, the first page is back. Each age has its own two
  pages. Items are assigned in the game's own item screen: opened with the chord button held, it
  shows and edits the second page.
- The chord button is pad bit 12, which no 3DS button sets and the game ignores. The app holds it
  from the left stick's click and the 3 key (app.md). The plan said ZL, but The One Screen already
  uses ZL and ZR for the LB and RB items, and every other button has a job.
- The item screen's layout in the save owns the buttons (decomp/oot3d `docs/item_buttons.md`):
  the game derives the equipped items and the HUD from it whenever the screen opens. So the pages
  trade places there. The page that is away sits in the screen's grid like any unassigned item;
  18 bytes of the save that read zero in the files seen remember which grid items it is. Every item stays in the layout,
  so a file played without the patch shows all of them and loses nothing.
- The pages trade only in plain play (the HUD up, no cutscene, no scene change); a cutscene or a
  scene change puts the first page back at once, because those can change Link's age.
- An item moved by hand from the grid to a button while its page is away leaves that page: an
  item is on the page it was put on last. With the grid full, a button with nowhere to put its
  item keeps it on both pages.
- The file always holds the first page on the buttons: the first page is put back at the start of
  the game's save routine (`Sram_WriteSave`), so a save made with the chord button held, or by
  autosave, is the same file as one made without it.
- Option: "Second item page", on by default (`patch/oot3d/options.toml`).

### Status (OoT3D, 2026-11-14)
Built (`patch/oot3d/chord_items.cpp`). Seen on the harness, File 1 as a child with a Deku Stick,
Deku Nuts and the Slingshot poked into the inventory (run `oot-ch7`, Kokiri Forest):
- First page: Deku Nuts assigned in the item screen; the HUD draws them on the button.
- Chord held: the HUD's four buttons empty; the item screen shows the nuts back in the grid;
  the Slingshot assigned there appears on the HUD button and that button aims it.
- Released: the nuts are back on the HUD and the item screen shows the Slingshot in its grid
  cell; held again: the Slingshot is back.
- Held across a scene change (`oot-ch6`): the first page is in during the change and the second
  returns after it.
- Saved and reloaded (`oot-sv1` to `oot-sv7`): the 18 bytes are in `save00.bin` at 0x6C. A file
  saved with the second page away loads with the nuts on the button and the Slingshot on the
  chord (`oot-sv2`); the same file without our patch shows the stick, the nuts on Y and the
  Slingshot in the grid, nothing lost (`oot-sv3`). Saved with the chord held, the file has the
  first page on the buttons (`oot-sv7`); before the save hook it had the second (`oot-sv4`), which
  the patch still put right on load (`oot-sv5`) and the unpatched game showed with every item
  present (`oot-sv6`).
- The same item on both pages (`oot-ch8`): the nuts assigned on the chord page stay there and
  their first-page button is empty on release.
- Adult (`oot-ch8`, age poked before a warp): the adult layout and the adult half of the 18 bytes
  are used; nuts assigned on the adult chord page show on the HUD with the chord held and the
  adult first page is empty on release. A round trip of the age keeps the child pages (`oot-ad1`,
  `oot-ad2`).
- The age change itself (`oot-ag7`, `oot-ag8`): a child with nuts on the first page and the
  Slingshot on the second pulls the Master Sword at the pedestal (entrance 0x2CA, A on "Grab").
  Through the pull and the Chamber of Sages the buttons do not trade, also with the chord button
  held. Adult Link stands in the Temple of Time with the Master Sword on B and no items on either
  page; the child's pages are still in the 18 bytes.
- Adult assignments in the item screen (`oot-ag10`, `oot-ag11`): nuts put on Y on the first page
  by touch, bombs on X on the second page with the chord held; the HUD shows the nuts, the bombs
  while held, the nuts again on release.
- Not seen: the way back to a child through the pedestal (Sheik stands on it until the Forest
  Temple is done; the age set back by hand keeps the child's pages, `oot-ad2`), a real pad's
  stick click, a full grid.

### Status (MM3D, 2026-11-14)
Built (`patch/mm3d/chord_items.cpp`), human Link only, option "Second item page" on by default.
- MM3D keeps an item and its inventory slot per button and leaves the item in the Items screen's
  grid (decomp/mm3d `docs/items.md`), so a page is four slots. The page that is away sits in the
  slot bytes of the equipment's second row, which read 0xFF in a fresh file; the item is read
  back from the inventory by slot when the page returns, so a bottle's new contents or a lost
  item are followed.
- The pages trade from a hook in `Actor_UpdateAll`, in plain play only: no cutscene, no scene
  change, the HUD up and no text box. Anything else puts the first page back on that frame.
- The file always holds the first page on the buttons: a hook at the entry of the game's statue
  save routine (`Play_SaveAtStatue`, decomp/mm3d `docs/save.md`) puts it back before the file is
  built. The save prompt is a text box, so the first page is already back by then; the hook is
  the guarantee that does not depend on that.
- Seen on the harness (`mm-ch3`, South Clock Town, a bow and bombs poked into the inventory):
  the bow assigned to X in the Items screen; chord held, the HUD's buttons empty and the Items
  screen opened with it held shows them empty; bombs assigned to Y there show on the HUD; released,
  the bow is back on X; held again, the bombs on Y. The equipment bytes follow (`4D FF 01 FF FF`
  with slot 6 away, `4D 06 FF FF FF` with slot 1 away).
- Saved at South Clock Town's owl statue (`warp:0xD890`, wait out its tutorial cutscene, walk
  into the statue, A, A) with the bow on X of the first page and bombs on Y of the second:
  with the chord held through the save (`mm-sv-held`) and released before it (`mm-sv-rel`) the
  two `save0.bin` files hold the same equipment (`4D FF 01 FF FF`, slot 6 away in the second row)
  and differ only in the checksum, the clock's seconds and 20 random digits that differ per run.
- That file loaded without our patch (`mm-ld-plain`, `code.ips` moved aside, 0x317000 reads the
  game's own instruction): the bow on X, the bow and the bombs both in the Items grid. Loaded with
  the patch (`mm-ld-patch`): the bow on X, the bombs on Y while the chord is held.
- Deku, Goron and Zora (`mm-forms2`, form poked before a scene load, bombs away in the second
  row): each form's HUD and Items screen show the first page's bow on X and nothing on Y, the chord
  does nothing, the second row's bytes stay as they were and a write watch on the slot bytes of
  rows 1 to 3 reports no store from the game. Back as human the chord brings the bombs.
- Not shown: the hook finding the second page in (the text box always gets there first), the
  second statue type (0x4BFAB4 calls the same routine), a real pad's stick click.

## When
After the title picker; free camera first (OoT3D), then the chord buttons, then the glyphs.
