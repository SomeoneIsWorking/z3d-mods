# Replay interpolation (OoT3D and MM3D)

Both games tick their logic at 30 Hz. The goal is 60 Hz (and higher) presents whose in-betweens are
the game's own draw code run again with its moving inputs blended between two ticks, inside a
sandbox that rolls back every write. The method and its rules are the `replay-interpolation`
skill; this plan holds what is particular to these games and to Azahar.

Prior art (decomp/oot3d and decomp/mm3d `docs/prior-art.md`): TriAevum (GPL-3.0, OoT3D) and
MM3D-PC on its runtime interpolate by decomposing matrices found in the PICA vertex shader's
uniforms; that is evidence only and not this method. Shipwright and 2 Ship 2 Harkinian (N64)
record each matrix as it is drawn; their camera "don't interpolate" call sites are leads for the
cuts.

## What is known

- OoT3D: decomp/oot3d `docs/frame.md`. `Gameplay_Update` runs the tick's logic and then
  `Gameplay_Draw`, which builds the tick's PICA command list; the frame end waits two vblanks and
  runs the list, and the lists are double-buffered.
- MM3D: not traced yet; the same engine shape is expected (`Play_Update` is named).

## Design

1. **The draw section.** OoT3D: `Gameplay_Draw(play)` (0x2E25F0) from its call in `Gameplay_Update`; the list
   it appends to is the bound command list, so a replay's output is the list's contents from the
   section's start offset to its end. Still to RE: whether the section rebuilds everything it
   draws (no append across ticks), which calls reach a device (nngx locks, GX syncs, SVCs) inside
   it, and the HUD and the touch screen's share of the list.
2. **Running it in Azahar.** The core gets a replay owner beside the CPU: after the real tick's
   draw returns and before the frame end runs the list, it saves the ARM11 context, writes the
   blended tracks, calls the section on the game's thread with interrupts and guest time held,
   takes the list it wrote, and restores. An SVC, a GSP or HW register access inside the replay
   refuses that in-between.
3. **The sandbox.** Every guest store during a replay is journalled and undone. Azahar's dynarmic
   uses a fastmem page table, so stores do not pass a callback; the candidates are marking the
   game's writable pages as special (the rasterizer-cache path, which already routes accesses
   through the slow path) for the replay's duration, or write-protecting host pages and
   journalling on the fault. Measured before choosing.
4. **The GPU.** The replay's list runs through the same PICA command processor into its own
   render target, then the transfer the frame end does for a real list (render target to display
   buffer) into the in-between's own present image. The display buffers the game swaps are not
   touched.
5. **Pacing.** The frame end waits two vblanks per tick; the app presents the real frame on one
   and the in-between at t = 0.5 on the other, then more at higher refresh.
6. **Motion tracks.** Camera from the inputs `View_LookAt` (0x2D77DC) takes (eye, at, up) and the
   fov, blended unshaken with the new tick's quake added; actors by world position and rotation
   keyed by their address and the actor list's liveness; skeletons by joint matrices after the
   animation; then effects, particles and texture scroll as frames show them unblended.
7. **Cuts.** From the game's own camera resets, scene changes, warps and cutscene shot changes,
   found per game; the N64 ports' call sites are where to look first.

## Steps

1. OoT3D: the draw section's census (what it writes, what device it reaches), recorded in
   decomp/oot3d `docs/`.
2. The sandbox and the replay owner in the core; proof: with nothing blended (t = 1) the replay's
   list is byte for byte the real one, and a run with replays on and off reaches the same logic
   state after N ticks.
3. The GPU path for the replay's list and the 60 Hz presenter; the camera track; Kokiri Forest with
   the camera turning, crops of consecutive presents halfway between.
4. Actors and skeletons, then the pools; cuts.
5. MM3D: its frame loop and draw section, then the same tracks.
6. The option in each game's options ("Frame interpolation"), on by default once both games pass.

## Status

Not started beyond the RE above (2026-11-17).
