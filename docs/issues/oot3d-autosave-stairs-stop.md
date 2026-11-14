# OoT3D stopped on entering Kakariko with autosave and stepped stairs both on (fixed)

Observed 2026-11-14 on the harness (child, File 1 copy): with "Autosave after each scene
change" and "Stepped stairs" both on, the core stops (`core stopped (status 17)`) during a warp
into Kakariko after six or seven autosaves. Expected: the scene loads.

Reproduce: `z3dmods run oot3d <name> --harness wait:20 start wait:3 a wait:2 a wait:8
option:autosave,1` then `warp:<e> wait:12` over 0xEE, 0xDB, 0x108, 0x53, 0xCD, 0x157, 0xEE,
0xDB: it stops on the eighth (`oot-as13`, `oot-as16a`). Alternating 0xDB and 0xEE it stops on
the seventh warp, the fourth entry into Kakariko (`oot-as16b`).

What does not stop:
- the same warps with autosave off and stairs on (`oot-as15a`, `oot-as15b`: four entries);
- autosave on and `--option stairs=0`: 36 warps, 34 saves (`oot-as17`).

Every save before the stop is written (the save count rises by one per scene) and the file
loads afterwards.

## Cause
`patch/oot3d/autosave.cpp:Autosave::update` called `Sram_WriteSave` and never the
`Sram_FinishWrite` its callers in the game run once the write is done, so every autosave left
its 0x118 byte write request allocated in the system arena (decomp/oot3d `docs/save.md`).

Each leaked block sat at the top of that scene's allocations and split the arena's free space
(`oot-k4`: one more 0x120 block per visit, holding `data:/save00.bin`). A room takes one block
of three times its file's size: the stepped Kakariko room 0x432E70 bytes, the ROM's 0x2A8F70.
On the fourth entry no hole that large was left, the allocation returned 0 and the game's
loader read the room file to address 0 and on over its own code (`oot-k1`: 1 MB of unmapped
writes from address 0, then a fatal error). The ROM's smaller rooms still fit, which is why
36 warps passed with the stairs off.

Not the collision: the lists were never short of room (docs/plans/oot3d-stairs.md).

## Fixed
Autosave polls `Sram_IsWriteDone` and calls `Sram_FinishWrite`, as the pause screen does, and
starts no save while one is being written. `oot-k6`, autosave and stairs on, Kakariko and
Kokiri Forest in turn: 17 warps, 17 saves (count 4 to 21), the arena's blocks the same on
every visit (629 in Kakariko, 625 in the forest), Link standing on the stepped flight by the
well after the ninth entry.

Read an arena with the `arena:<address>` step: 0x565500 the system arena, 0x5A2E3C the Zelda
arena (decomp/oot3d `docs/notes/arena.md`).
