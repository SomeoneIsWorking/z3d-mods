# OoT3D QoL in our patch: Quick Text and optional autosave

User: "Oh don't forget about the QoL mods"; on the CC BY-NC-ND mods: "I'd rather make MIT versions
of those then".

- **Quick Text**: text prints at once and can be advanced at once, written by us; the OoT3D Randomizer's
  version is evidence only (no licence for its own code, docs/mods.md).
- **Autosave, optional**: after a scene transition, save through the game's own save routine, as
  the in-game save does. Off by default and written from scratch, not from the CC BY-NC-ND autosave mod.

Each has a switch in the game's options menu (`patch/oot3d/options.toml`): Quick Text on, autosave
off by default.

Both go into the same MIT `code.ips` as the cutscene skip ([cutscene-skip.md](cutscene-skip.md)).

## Status
- Quick Text, built (`patch/oot3d/quick_text.cpp`): at the start of every message and after every
  box break the patch calls the text object's own instant-on method (what control code 0x04 does),
  and code 0x05 (instant off) is skipped. Delay codes still pause, so timed narration keeps its
  pacing. RE: `decomp/oot3d/docs/message.md`. Seen 2026-11-13 in the Deku Tree's scene after Gohma
  (every box whole on its first visible frame, the choice too) and in the opening narration.
- Autosave, built (`patch/oot3d/autosave.cpp`, option "Autosave after each scene change", off by
  default): when a scene change that began in play has finished (the fade in over, no cutscene,
  the HUD up), the patch calls the game's own `Sram_WriteSave(play, 0)`, the call the pause
  screen's Save makes (decomp/oot3d `docs/save.md`). The file is the one a manual save there
  would write: Link's place on load follows the game's own rules, not the spot of the save.
  Loading a file does not save; only leaving a scene does.
  Seen 2026-11-14 (`oot-as2`, `oot-as3`): with the option off a scene change writes nothing;
  with it on, entering the Know-It-All Brothers' house writes the file (save count 4 to 5, the
  saved scene the house, 99 rupees poked in before); after a restart the file loads with the 99
  rupees.
- Not seen: an autosave in a dungeon, as an adult, after a death or a void-out, or over a long
  session; the save runs on the game's own thread while play goes on, where the pause screen
  waits for it.
