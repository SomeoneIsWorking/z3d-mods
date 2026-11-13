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

## Open questions for RE (decomp/oot3d first)
- How OoT3D marks those ramps: the collision polygon's surface type (not decoded yet), the room
  CMB's material (the stair texture), or neither (then a per-scene list of polygons found offline
  from the collision and room files).
- Whether OoT3D's own models already have real steps in places where N64 had ramps; those are left
  alone.
- Where the patch hooks: a room mesh draw (and, for the optional climb, the collision line test),
  through generated constants.

## When
After the title picker, then the coordinator's free camera, chord items and glyphs.
