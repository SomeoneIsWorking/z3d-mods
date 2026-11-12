# OoT3D QoL in our patch: Quick Text and optional autosave

User: "Oh don't forget about the QoL mods"; on the CC BY-NC-ND mods: "I'd rather make MIT versions
of those then".

- **Quick Text**: text prints at once and can be advanced at once, ported from the OoT3D Randomizer
  (MIT, attributed).
- **Autosave, optional**: after a scene transition, save through the game's own save routine, as
  the in-game save does. Off by default and written from scratch, not from the CC BY-NC-ND autosave mod.

Quick Text comes with the skip. The MIT autosave comes after it; until then the existing OoT3D
Autosave (CC BY-NC-ND, `code.ips` only) is baked into the base `code.bin` beside The One Screen
when their writes do not overlap.

Both go into the same MIT `code.ips` as the cutscene skip ([cutscene-skip.md](cutscene-skip.md)).
