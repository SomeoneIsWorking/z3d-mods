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
