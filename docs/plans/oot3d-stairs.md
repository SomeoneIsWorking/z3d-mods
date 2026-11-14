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
  `docs/notes/scene_collision.md`). `stairs.collision` takes each sloped floor triangle that lies
  on a drawn stair ramp (all three corners within 4 units of it) and folds it with the same
  cuts as the drawn steps, reading the step positions from the drawn triangle under it: flat
  treads and vertical risers, each inheriting the ramp triangle's surface type and flags. The
  steps' end faces are left out: they are lower than the height the game checks walls at. The
  curbs stay ramps, as drawn.
- The longer vertex and triangle lists are written after the end of the scene file and the
  collision header pointed at them; nothing else in the file moves. `z3dmods install oot3d`
  writes the seven scenes beside their stepped rooms.
- Counts: Kakariko 1222 → 5581 triangles and 786 → 4601 vertices (a triangle can name 8192);
  Zora's Domain 1421 → 2306; Ganon's Castle 5434 → 7349. Desert Colossus's stair shapes have no
  collision triangle on them, so nothing changes there.
- Shown in game (`oot-st1`, Kakariko, the flight by the well): dropped onto the flight every 4
  units along it, Link lands at 42, 50, 57, 65, 72 and 80, three or four drops on each, where the
  ramp gave a new height every drop; walked up it, his height goes 35, 42, 50, 57, 65, 72, 80
  without stopping at a riser. `oot-st2`, Zora's Domain's rock stairs: 417, 427, 437, 447, 457.
- Not seen: the other five scenes, an adult, Epona or a rolling bomb on the steps, enemies.

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
- Collision: written for `z2_danpei`, `z2_miturin` and `z2_tenmon_dai`, the three scenes whose
  collision triangles lie on the drawn ramps. The Astral Observatory loads and plays with it
  (`mm-st4-1`: the passage, the water), but Link was not walked up a flight and no height was
  read: the run steps have no MM3D player position (`teleport` and `where` are OoT3D only).
- Still open: the other ten scenes keep the ramp's collision, so Link walks the old slope through
  the drawn steps there. Their collision is coarser than the drawn ramp (two triangles reach 14
  units past the flight's side in `z2_kindan2`; none stays near the flight in `z2_yadoya`), so
  the triangles have to be cut at the ramp's outline before they can be folded. The player's
  position for the MM3D run steps, then heights on a flight in each of the three scenes.
