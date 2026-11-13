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
  stepped copies of those rooms into the romfs mod (`"stairs": true` in `sets/oot3d.json`),
  generated from the player's ROM and never committed.
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

## Still open
- Collision is the ramp's: Link walks the pitch line, up to one step above the tread under him.
- Our own stair texture: the steps use the game's (or the pack's) painted texture.
- The per-game options toggle.

## Majora's Mask 3D
Rev 1 room files (`scenes/*_<n>_info.zsi`, all uncompressed) name these stair textures:
`kindan2_kaidan`, `kindan2_kaidanw` (z2_kindan2 room 1), `dkh_step_01`, `dkh_step_suna_0`
(z2_danpei rooms 5, 6, 8), `Z2mitu_step_00`/`_01` and `e1StEpc` (z2_miturin rooms 3, 10),
`hak_r02_step_01` (z2_hakashita 2), `om_step_01` (z2_omoya 1), `tenmon_step_01`/`_02`
(z2_tenmon_dai 0, 1), `yado_step_01` (z2_yadoya 0). Whether each is a painted ramp or modelled
steps needs the geometry check; MM3D's CMB is version 10 (an extra `qtrs` chunk, a tangent slice)
and keeps textures in `.ctxb` bundles, so `cmb.py` needs that layout first.

