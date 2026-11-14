# Real stairs (OoT3D)

User: "I would also like the stairs mod I had in SomeoneIsWorking/zelda3d but that repo is deleted
and can't be restored". Asked whether ramps drawn as stairs become visible steps, whether Link
climbs each tread, whether it used its own stair texture, and which game: "yes yes yes oot"; then,
on Link climbing each tread: "this is optional".

The original lived in the deleted Shipwright-based PC port (stair-tread generation in
`soh3d.c`/`soh3d_model.cpp`, an SVG stair texture embedded as PNG). No source survives, so this is
our own MIT patch, written fresh. Searched on 2026-11-13: the Claude, Codex and opencode histories,
Claude's file history, coredumps, the journal, caches, SomeoneIsWorking/oot3d-decomp (the old
project's RE notebook) and GitHub (the Shipwright fork is gone too). What survives is the ground it
stood on, now in decomp/oot3d `docs/notes/scene_collision.md`: the OoT3D scene collision format
(polygons, normals, plane distances) and the one-CMB-per-room mesh layout, both checked across six
scenes.

- Where the game has a ramp drawn with a stair texture, the player sees real treads: geometry
  generated from the ramp's polygon, each tread with its riser.
- Optional, after the visible treads: collision that follows them, so Link climbs step by step
  instead of up the ramp. Without it the ramp's own collision stays.
- The treads use our own stair texture, drawn as an SVG and embedded as a texture.
- A toggle in the per-game options menu, on by default.

A target from the old project's screenshots: Kakariko Village's main staircase below the windmill,
a flat ramp drawn with a stair texture (SoH, 2026-06-17, before the stairs work).

## Built (2026-11-13)
- The ramps are marked by their room CMB material: textures named `*kaidan*` ("stairs"), in nine
  rooms (Kakariko, Zora's Domain 0/1, Desert Colossus, Gerudo Valley, Hyrule Castle grounds, Death
  Mountain Trail, two Ganon's Castle rooms). No code hook is needed: `z3dmods install oot3d` writes
  stepped copies of those rooms and their scenes into `load/mods/<title id>/romfs_stairs/`
  (`"stairs": true` in `sets/oot3d.json`), generated from the player's ROM and never committed.
- The switch is "Stepped stairs" in the game's options menu, on by default: a layer option
  (`layer = "romfs_stairs"` in `patch/oot3d/options.toml`, docs/app.md "Options"). While it is on
  the app has the core put that folder over the mod's `romfs/`; off, the game loads the ramps.
  Seen 2026-11-14 on Kakariko's well flight (x 484 to 502 in steps of 3, z 590): on, Link lands
  at 57, 57, 57, 65, 65, 65, 65 (`oot-so3-1`); with `--option stairs=0` at 60, 61.5, 63, 64.5,
  66, 67.5, 69 (`oot-so3-0`). The row shows in the options menu (`inputtest options-oot3d`).
- `tools/z3dmods/stairs.py` reads where the texture paints its steps (row period, nose edge, crease)
  from the texture the player sees: the set's 4K pack replacement when it has one
  (`texture_pack.py` finds it by Azahar's texture hash), else the ROM's. The ramp is the pitch line through the
  step noses, as on real stairs: faces between 10 and 60 degrees are cut at each painted crease and
  nose and folded into vertical risers and flat treads below the ramp, each nose on it. Nothing
  rises above the ramp, so steps stay inside the walls beside them (a version centred on the ramp
  poked its corners out of Kakariko's rock walls). The texture's stripe-free edge columns stay flat
  on the ramp as curbs along both sides, as the vanilla flights paint them; two-sided side panels
  close the steps up to the curbs and the open edges. The painted shading lands on the matching
  face because the cuts follow the texture's own v and u.
- Shown in game: Kakariko's intro cutscene, frame 140 (`warp:0xDB csframe:140`), the flight by
  the well has a stepped silhouette where vanilla has a straight ramp edge, with the 4K pack on.
- A primitive set holds at most 32767 indices; more crashes the game (decomp/oot3d
  `scene_collision.md`). `cmb.py` spreads a larger shape over copies of its primitive set:
  Kakariko's 40188 indices draw as two sets. Shown in game: `stairs-video4`, teleported to the well
  flight (515,100,627) and the upper flight (-464,120,-56), orbiting and walking each.

## Collision (2026-11-14)
- The scene file (`scene/<scene>_info.zsi`) holds the collision for all its rooms (decomp/oot3d
  `docs/notes/scene_collision.md`). `stairs.collision` works in three passes:
  1. The drawn ramp triangles are joined into convex outlines (same plane, same painted steps),
     and the scene's sloped floor triangles into convex pieces (same type, flags and plane). Each
     piece is cut at the outlines seen from above: a part over an outline and within 4 units of
     the drawn ramp becomes steps, the parts beside it stay as they were. The collision is
     coarser than the drawn ramp in most scenes (one triangle can span a flight and the floor
     beside it), which is why it is cut and not picked by its corners.
  2. The parts over a ramp are folded with the same cuts as the drawn steps, the step positions
     read from the drawn ramp under them: flat treads and upright risers, each with the ramp
     triangle's surface type and flags. No step folds below the flight's foot. The faces that
     close a step up to the ramp where no other step continues it are kept when they face up
     (they fill the plan a step leaves at the ramp's ends) and dropped when upright.
  3. Walls and ceilings lose their parts between a ramp and one step below it. The ROM had
     solid ground there, so its walls end anywhere in it: in the Stock Pot Inn the upper floor's
     ceiling and the wall under it reach 4 units below the ramp and stopped Link on the last
     tread but two (`mm-st13-yadoya`).
- Every step face the game takes for a wall (steeper than 60 degrees) is flagged so actors
  pass it (the first corner's 0x4000, decomp/mm3d `docs/formats/scene_files.md`): Link's feet
  rise with the floor from tread to tread. As walls, the risers of a steep flight stack up to a
  ledge: in the Milk Bar he hopped three steps at a time until they were flagged. A narrow riser
  leans once its corners are integers, so the test is the game's, not "upright": a leaning one
  left unflagged stopped Link on Kakariko's flight to the mountain gate (`oot-st16-spot01`).
- The step faces of one plane are joined back into whole treads and risers, and a curb is not
  cut at the steps beside it. The game keeps a list node for every triangle in every grid cell
  it touches and does not check the list's end (decomp/oot3d `docs/notes/scene_collision.md`):
  uncut, Kakariko needed 54760 nodes of 36097 and the game stopped on the second visit
  (`oot-bg1`, `oot-as10`). Joined it needs 28584 of 37914; Ganon's Castle 17944 of 23197; every
  other scene of either game is under half. `install` does not compute this: a new scene or a
  finer texture has to be read in game (`peek:play+0xA98,0x50` in OoT3D, `play+0xAB0` in MM3D;
  the u16 at +0x44 is the list's length, at +0x46 the nodes used).
- A ramp is stairs only when its painted steps rise 3 to 20 units each. The Astral
  Observatory's entrance has a plank ramp with a `step` texture that paints a 59 unit "step"
  and six small ramps whose rows run up the slope; stepped, the first stopped Link
  (`mm-st13-tenmon0`). Desert Colossus has eight triangles at 24 units. They stay ramps, drawn
  and walked.
- The longer vertex and triangle lists are written after the end of the scene file and the
  collision header pointed at them; nothing else in the file moves. A triangle names its
  corners in 13 bits, so a scene holds at most 8192 vertices: Kakariko has 7869.
- A shape's indices are 16 bit and a primitive set holds at most 32767 of them (`cmb.py`
  spreads a larger shape over several sets, each starting within the first 65536 indices).

- Measured 2026-11-14 on the layer as committed. Each flight: Link dropped along it every few
  units (the heights he lands on), then walked from its foot to its top with the circle pad.
  - Kakariko (`oot-st17-spot01`): eleven flights, 256 drops on 67 heights, every walk reaches
    the top; the frames show the stepped flights by the well, the windmill, the shop and the
    mountain gate, Link standing on treads. The two long flights outside the gate (x below
    -2700) have no floor in the ROM either: Link falls through them with the option off too
    (`oot-kk0`, `oot-kk1`).
  - Zora's Domain (`oot-st17-spot07`): 26 heights 10 apart, both flights walked. Desert Colossus
    (`oot-st17-spot11`), Gerudo's Fortress (`oot-st17-spot12`), Ganon's Castle and its collapse
    (`oot-st17-ganontika`, `oot-st17-sonogo`, steps 13.4 apart): walked. Hyrule Castle
    (`oot-st17-spot15`): steps 7 apart, the walk ends on the top landing 4 units beside its
    target. Death Mountain Trail (`oot-st17-spot16`): the short flight by the Kakariko exit
    lands on 1181 to 1201; the drop next to the exit left the scene, so the long upper flight
    is not measured.
- Not seen: an adult, Epona or a rolling bomb on the steps, enemies, arrows against a riser.

## Still open
- Our own stair texture: the steps use the game's (or the pack's) painted texture. The eight
  stair textures differ by place (wood, dark rock, sandstone, moss); one drawn texture would
  replace all of them, so this waits for the player's word.

## Majora's Mask 3D (2026-11-14)
Same owner and switch as OoT3D: `"stairs": "kaidan|step"` in `sets/mm3d.json` is the pattern a
stair texture's name matches (OoT3D's is `kaidan`; its `*step*` textures are left alone), the
install writes the stepped files to `romfs_stairs/`, and "Stepped stairs" is in MM3D's options.
File facts (LzS wrapping, CMB version 10, the collision header) are in decomp/mm3d
`docs/formats/scene_files.md`.

- Geometry check: every stair-textured mesh is a sloped quad ramp, 15 to 45 degrees, none
  modelled as steps. 17 rooms in 13 scenes: `z2_clocktower` 0, `z2_town` 0, `z2_ichiba` 0,
  `z2_backtown` 0 (the four Clock Town districts), `z2_milk_bar` 0, `z2_musichouse` 0, `z2_danpei`
  5, 6 and 8, `z2_hakashita` 2, `z2_kindan2` 1, `z2_miturin` 3 and 10, `z2_omoya` 1,
  `z2_tenmon_dai` 0 and 1, `z2_yadoya` 0.
- Drawn steps, seen (`mm-st3-1` against `mm-st3-0` with `--option stairs=0`, South Clock Town,
  `warp:0xD800`): the flight beside the tower has risers, treads and a stepped edge; off, it is
  the flat painted ramp. East, West and North Clock Town load from their stepped rooms (`mm-st2`).
- Collision: every scene with a floor under its drawn flights, by the same three passes as
  OoT3D. The first version (c3b4fef) wrote OoT3D's triangle layout into MM3D's scenes, which
  dropped Link through the Observatory's stairs (`mm-st5-1`); the layout is per game now
  (decomp/mm3d `docs/formats/scene_files.md`).
- Measured 2026-11-14 (`mm-st15-<scene>`, drops and a walk up each flight; node use under 37%):
  Stock Pot Inn (steps 13 apart, walked to the upper floor), Milk Bar, Music Box House, Ranch
  house (both flights), Astral Observatory rooms 0 and 1 (32 heights 7.5 apart, four flights
  walked), Woodfall Temple (four flights), Ikana Graveyard's Dampe passage (30 steps of 13.3),
  Ancient Castle of Ikana, Beneath the Graveyard: all walked without a stop. The frames show
  stepped stairs with Link on a tread in the Inn, the Milk Bar, the Music Box House and the
  Observatory; in the dungeons the flight's room is not loaded from the entrance used, so those
  frames are black and only the heights speak.
- South Clock Town (`mm-st15-clocktower`): drops land 4.2 apart (39, 44, 48, 52, 56). Not
  walked: Tatl's first-day talk holds Link there and the runs did not get past it.
- East, West and North Clock Town draw every district's flights, but only their own have a
  floor; West's one flight lands on 8 heights (`mm-st15-ichiba`), the other two have none.
