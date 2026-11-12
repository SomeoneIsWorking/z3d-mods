# Press-Start cutscene skip (OoT3D and MM3D)

User: "a cutscene skip mod (by pressing start), I don't think there is a skip mod but we can make one".

## What it does
During a skippable cutscene, Start ends it: the game lands in the state it would reach by watching
to the end (flags, items, time of day, entrance, music), with no other change.

## Design
- Our own MIT `code.ips`, written from scratch, linked with clang/lld (`armv6k`, `mpcore`) against
  the vanilla addresses. It rides on the set's baked `code.bin` (see `docs/mods.md`, "How Azahar
  2126.2 stacks mods"), so it never links GPL or third-party code.
- The installer refuses to install if any of our write ranges overlaps a byte the base patch changed.
  Our payload goes after the base payload in `.bss`, and the installer grows `.bss` in the base
  exheader to cover it.
- The skip drives the game's own cutscene end: it advances the cutscene context's frame to its end
  frame, so the game's own end-of-cutscene commands (destination, flags, transition) still run. It
  does not set flags itself. Reference: OoT3D `csCtx = play + 0x2298` (state +0x08, end frame +0x18,
  current frame +0x20) from the player's oot3d-decomp `docs/cutscene_format.md`; MM3D's equivalent is
  to be found in mm3d-decomp.
- Start is consumed while a cutscene is skipped, so it does not also open the pause menu.

## Proof
On real cutscenes in each game, reached through a warp from a copy of the save: one run watches to
the end and one presses Start. Compare the save context (the flag arrays, scene, entrance, time) read
over Azahar's RPC after both land, and look at the frames.
