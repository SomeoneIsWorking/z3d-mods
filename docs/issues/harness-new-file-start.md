# Harness: Start on a just-created OoT3D file is ignored

Observed 2026-11-12 on the harness (core 1b86acc48): after a new file is named and "Save
completed" shows, the File 2 menu opens and moves its cursor, B backs out, but A or Start on
"Start" does nothing for 10 s or more, and no play state appears. A savestate and loadstate in the
same process does not help. File 1 (loaded from disk) starts normally, also after File 2 was made.

Expected: A on Start begins the opening, as on hardware.

Repro: `z3dmods run oot3d x --harness wait:20 start wait:4 down wait:0.5 a wait:2 start wait:1 a
wait:2 a wait:3 down wait:0.5 a wait:1.5 a wait:8 cs` prints "no play state".

Cause: not traced; the core logs nothing at the press. Not checked on standalone Azahar.
The opening is reached from File 1 instead (see docs/plans/cutscene-skip.md).
