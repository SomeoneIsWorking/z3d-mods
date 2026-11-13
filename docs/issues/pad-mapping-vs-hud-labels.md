# App: an Xbox pad's buttons do not match The One Screen's labels (OoT3D)

Read from the code on 2026-11-14, not yet tried on a pad.

The One Screen labels its HUD for a pad bound by name (its README): Xbox A/B/X/Y to the 3DS's
A/B/X/Y, LT/RT to L/R, LB/RB to ZL/ZR. Its HUD draws A at the bottom and B on the right, the
I and II item slots as LB and RB, and the Ocarina's L and R notes as LT and RT.

The app binds a pad by position (`app/src/pad_input.cpp` `kPadButtons`: east is A, south is B) and
puts the bumpers on L/R and the triggers on ZL/ZR. On an Xbox pad the button marked A then presses
the 3DS's B (the sword, drawn as B on the right), the button marked B does what the HUD draws as A,
the bumpers target and shield, and the triggers use the slots drawn as LB and RB.

Expected: the button a label names is the button that does it. For OoT3D that is The One Screen's
binding: A/B/X/Y by name, triggers on L/R, bumpers on ZL/ZR. Our own glyph textures
(docs/plans/xbox-glyphs.md) are drawn for that binding.

Fix: a pad binding per game in the app (`pad_input`), by name for OoT3D; MM3D's follows what
Project Restoration and its Xbox UI label. The keyboard bindings stay the player's.
