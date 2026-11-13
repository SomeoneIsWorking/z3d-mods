# OoT3D QoL in our patch: Quick Text and optional autosave

User: "Oh don't forget about the QoL mods"; on the CC BY-NC-ND mods: "I'd rather make MIT versions
of those then".

- **Quick Text**: text prints at once and can be advanced at once, written by us; the OoT3D Randomizer's
  version is evidence only (no licence for its own code, docs/mods.md).
- **Autosave, optional**: after a scene transition, save through the game's own save routine, as
  the in-game save does. Off by default and written from scratch, not from the CC BY-NC-ND autosave mod.

Quick Text comes with the skip. The MIT autosave comes after it; until then the existing OoT3D
Autosave (CC BY-NC-ND, `code.ips` only) is baked into the base `code.bin` beside The One Screen
when their writes do not overlap.

Both go into the same MIT `code.ips` as the cutscene skip ([cutscene-skip.md](cutscene-skip.md)).

## Status
- Quick Text, built (`patch/oot3d/quick_text.cpp`): at the start of every message and after every
  box break the patch calls the text object's own instant-on method (what control code 0x04 does),
  and code 0x05 (instant off) is skipped. Delay codes still pause, so timed narration keeps its
  pacing. RE: `decomp/oot3d/docs/message.md`. Seen 2026-11-13 in the Deku Tree's scene after Gohma
  (every box whole on its first visible frame, the choice too) and in the opening narration.
- Not yet: an option to turn it off (the per-game options menu), and the MIT autosave.
