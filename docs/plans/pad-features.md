# Pad features: right-stick free camera (OoT3D and MM3D)

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
Replaced by our own item slots, where LB with A, B, X and Y are slots 8 to 11
([pc-controls-hud.md](pc-controls-hud.md)); `chord_items.cpp` is gone from both patches.

## When
After the title picker; free camera first (OoT3D), then the chord buttons, then the glyphs.
