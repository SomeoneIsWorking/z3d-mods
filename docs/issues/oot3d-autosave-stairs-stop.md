# OoT3D stops on entering Kakariko with autosave and stepped stairs both on

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

Not the collision's list nodes: Kakariko uses 28584 of 37914 (docs/plans/oot3d-stairs.md); the
stop was the same before and after that was fixed (`oot-as10`).

Cause: not found. Next step: break on the stop in the app (the core's status and the guest's
last exception or `svcBreak`), and read the game's heap free size after each save with stairs
on and off; the stepped Kakariko room and scene files are several times the ROM's.
