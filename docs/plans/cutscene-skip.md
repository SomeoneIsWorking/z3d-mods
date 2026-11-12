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
- One press of Start ends the cutscene within the frame it is pressed: the player never sees the
  rest. Inside that frame the patch runs the game's own cutscene interpreter for every remaining
  frame up to the script's end frame, so every command the script would have issued (destination,
  flags, time of day, music, the stop command) runs in order, and the game ends the cutscene itself.
  It sets no flags of its own. Stepping stops early if a command holds the frame (a message waiting
  for the player) or a scene transition starts.
- OoT3D: `patch/oot3d/` replaces the interpreter `FUN_002c5ba0` at its two call sites in the
  cutscene update `FUN_00321f50` (0x322040, 0x322070) with `stepCutscene`. `csCtx = play + 0x2298`:
  state +0x08 (2 running), end frame +0x18, current frame +0x20. Start is `play + 0x18` bit 3 in
  normal play (`gameMode` 0), after the first second of the cutscene, with no transition pending.
  The game's own title-demo skip in the destination handler `FUN_00491364` also requires
  `*(0x5043D4 + 0x18) == 0`; that word is 1 during every gameplay cutscene, so ours does not.
- MM3D's equivalent is to be found in mm3d-decomp.

## Status
- OoT3D Kakariko intro (warp 0xDB from Link's house, misc stop at frame 255): Start at frame 57 ends
  it the same frame; the landing frame matches watching it to the end, and the save context differs
  only in `naviTimer` (+0x4C) and the counter at +0x15B4.
- Still to prove: a cutscene with a destination, one with textboxes, and MM3D.

## Proof
On real cutscenes in each game, reached through a warp from a copy of the save: one run watches to
the end and one presses Start. Compare the save context (the flag arrays, scene, entrance, time) read
over Azahar's RPC after both land, and look at the frames.
