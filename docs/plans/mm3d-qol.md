# MM3D QoL in our patch: Quick Text and optional autosave

The same two switches OoT3D has ([oot3d-qol.md](oot3d-qol.md)), written for Majora's Mask 3D's
own code, in the same MIT `code.ips` as its cutscene skip. Each has a row in the game's options
(`patch/mm3d/options.toml`): Quick Text on, autosave off by default.

## Quick Text
Built (`patch/mm3d/quick_text.cpp`). MM3D compiles a message into elements and a printer
reveals them (decomp/mm3d `docs/message.md`), so the shape is not OoT3D's layout hook and the
two share no code. Before every update in which the printer is printing, the patch calls the
printer's own instant-on method (what control code 0x27 does, and what the game does itself
for a message flagged to print at once, such as the owl statue's question). The rest of the box
then prints in that update. Delay codes still pause, and a box still waits for A.

Seen 2026-11-14, the carpenter in South Clock Town on the same frame after A: with
`--option quick_text=0` the box holds "Hey" (`mm-qt-off`), with the option on both lines
(`mm-qt-on`). With the guard's three-box message in the carpenter's entry (`mm-qt-box`): each
box is whole when it opens, the swamp in green, and the third shows "..." for half a second
before "A sword?", as its delay code asks.

## Autosave
Built (`patch/mm3d/autosave.cpp`, option "Autosave after each scene change", off by default).
When a scene change that began in play has finished (no cutscene, scene change or text box, the
HUD up), the patch saves as an owl statue does: `Play_SaveAtStatue(play, entrance)` with the
scene's own entrance (`CommonData.entrance`, its layer bits cleared as a statue's are), then
`SaveFile_StartWrite` on the play state's writer. The writer is polled with
`SaveFile_UpdateWrite` every update until it is idle, which frees the write's job, and no save
starts while one is being written (decomp/mm3d `docs/save.md`).

Which save: MM3D writes one file per slot, and its load reads `statueSave` from it.
- A statue save (`statueSave` 1) resumes the cycle where it was: the entrance and the clock as
  saved, the cycle's progress kept. MM3D's load does not delete it, so unlike the N64 game's owl
  save it is permanent: it loads again after every reset.
- The Song of Time's save (`statueSave` 0) is the cycle's: its load starts the cycle over at the
  dawn of the first day in South Clock Town. Made in the middle of a cycle it would keep that
  cycle's items and flags at the first dawn, a state the game never makes.
So autosave makes the statue save. Not during the final hours (from midnight of the third day):
a file saved there could leave no time to play the Song of Time before the moon falls, and the
clock tower's roof and the moon are entered from there.

Seen 2026-11-14:
- Off against on (`mm-q-as1`, a new file at South Clock Town's owl statue): with the option off
  a scene change writes nothing (save count 2); with it on, West and East Clock Town, the Laundry
  Pool and Termina Field each write the file once (count 3 to 6). The file on the card holds
  `statueSave` 1 and the entrance 0x5400; the writer is idle with no job after each.
- Many scene changes (`mm-q-as3`): 25 warps around Clock Town write 25 times (count 2 to 27),
  then Termina Field, then Link walked back through the gate into West Clock Town: a write each
  (29). The writer's job is freed after every write; the game plays on.
- The final hours (`mm-q-as5`, day and time poked): on the third day at 7:40 a scene change
  writes the file (count 3); at 1:30 that night the next writes nothing and the file keeps the
  daytime save.
- Reload in the window (`z3dmods inputtest autosave-mm3d`): a file given six hearts and saved by
  autosave on entering Termina Field boots from File 1 into Termina Field at the gate, with six
  hearts and the saved clock (6:29).
