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
  Off against on, 2026-11-14, a Know-It-All Brother's first line on the same frame after A:
  with `--option quick_text=0` the box holds "I c" (`oot-qt1`), with the option on the whole
  line and its three choices (`oot-qt2`). Link's bed is no test: its line is whole on its first
  frame either way (`oot-qt-0`, `oot-qt-1`, every frame recorded).
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
  Through a door (`oot-as6`): walking out of the house's door into Kokiri Forest writes the file.
  Out of a dungeon (`oot-as6`): walking out of the Deku Tree starts a cutscene in the forest;
  nothing is written while it runs, and the file is written once it has ended. Into the Deku
  Tree (`oot-as4`): nothing during its first-visit cutscene, the write after it.
  Boss room (`oot-as5`, Gohma's): one write as Link stands inside the door, in plain play before
  the boss appears; none during the boss's entrance or in the 25 s after it. That file loads at
  the Deku Tree's entrance (`oot-as7`), which is the game's rule for a save made anywhere in a
  dungeon and so what the pause screen's Save gives in the same room (same routine; the pause
  screen's Save was not pressed in the boss room to compare).
  As an adult (`oot-as11`, age set before a warp): the Temple of Time, Kakariko and Zora's
  Domain each write the file (save count 4 to 7); that file loads as adult Link in the Temple
  of Time with the Master Sword (`oot-as11r`).
  A void-out (`oot-as9`, Link put under Kokiri Forest): the respawn is a scene change and
  writes the file, with the heart the fall cost gone.
  A death (`oot-as9`, health set to 0): the game itself writes the file on Game Over, with
  autosave off too (`oot-as12`, count 4 to 5); with it on, the respawn after Continue writes
  once more (count 6 to 8) with three hearts, and that file loads (`oot-as9r`).
  A long session with the stairs off (`oot-as17`): 36 scene changes, 34 writes (the first two
  came while the one before was still fading in), no stop; the file loads.
  With the stepped stairs on (`oot-k6`): 17 scene changes between Kakariko and Kokiri Forest,
  17 writes, no stop. Before the write request was freed the game stopped there on the fourth
  entry ([issues/oot3d-autosave-stairs-stop.md](../issues/oot3d-autosave-stairs-stop.md)).
- The write request is ours to free: autosave polls `Sram_IsWriteDone` and calls
  `Sram_FinishWrite`, and starts no save while one is being written.
- The save runs on the game's own thread while play goes on, where the pause screen waits for it.
