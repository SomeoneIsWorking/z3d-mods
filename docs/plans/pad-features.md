# Pad features: right-stick free camera and chord item buttons (OoT3D and MM3D)

User: "Right stick free cam + chord buttons for item uses (on top of Y and B) + Xbox glyphs +
whatever you can think of". Glyphs: [xbox-glyphs.md](xbox-glyphs.md). Each feature is our own
code in the MIT patch, with a toggle in the per-game options menu, on by default.

The app feeds the host's right stick to the 3DS C-stick and the triggers to ZL/ZR
(`app/src/pad_input.cpp`), so both features read the New 3DS inputs in the guest.

## Free camera
- OoT3D never reads the C-stick. The patch reads it through `ir:rst`, with libctru's client
  (zlib, copied with attribution).
- The orbit is ours: yaw and pitch from the stick, distance from Link's age, eye pulled in front
  of collision, the game's camera setting kept current so scene cameras still switch. It hands
  control back on Z-targeting, first person, cutscenes, crawlspaces, horses and fixed-camera
  settings. L recenters behind Link, as Z-targeting does. Options: invert X, invert Y.
- Hook: `Camera_Update`, where the main camera's per-frame update would run. The OoT3D
  Randomizer's free camera (`code/src/camera.c`, no licence for its own code) hooks there and is
  evidence only, as are Ocarina ReFramed (GPL-3.0) and Roberto-Nessy's Free Cam (no licence).
- RE first, in decomp/oot3d: the `Camera` fields the orbit reads and writes (camDir, inputDir,
  setting, status, player, play), their offsets checked against the binary, into `z3Dcamera.hpp`.
- MM3D already turns the camera with the C-stick on New 3DS; the patch adds the invert options
  and the recenter button there only if the game's own does not cover them.

## Chord item buttons
- While ZL is held, X/Y/A/B use four extra item slots; the normal X/Y (and MM3D's I/II) items stay
  as they are. Slots are assigned in the game's own item menu: with ZL held, the button that
  assigns an item to X/Y assigns it to that chord slot instead.
- The HUD shows the chord slots while ZL is held, with our own glyphs.
- RE first: the item-button use path and the item menu's assignment in both decomps, and whether
  either game already reads ZL (then the chord takes ZR).

## When
After the title picker; free camera first (OoT3D), then the chord buttons, then the glyphs.
