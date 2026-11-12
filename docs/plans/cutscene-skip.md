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
  It sets no flags of its own. User: "don't fast forward please, skip entirely".
- Text: a text command (0x13) holds its end frame while `Message_GetState()` (0x3769D8) is not
  0, 2, 7 or 8 (0x2C7AD4). On a hold the skip calls `Message_CloseTextbox(play)` (0x3725E0) once,
  stops stepping for that game frame, and resumes on the next frame once the game's message update
  has finished closing (manager byte +0xF38 = 0x0E, counter +0xFA4 below 2). Closing again every
  frame resets that counter and stalls the cutscene. A choice (state 4) is never closed: the skip
  waits there for the player.
- Transitions: `play + 0x5C2D` is 20 when a scene transition is requested, -20 during the incoming
  fade (it stays -20 through the black opening narration until frame ~160), 0 otherwise. The skip
  refuses to start, and stops stepping, only at 20.
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
- OoT3D opening narration (File 1 copy, `poke:0x588EF8,f1ff warp:0xBB`: nextCutsceneIndex 0xFFF1
  into Link's house, as a new file does; script 0x0877CE84, end 4500): Start at frame 74, with the
  first box on screen, closes each box, runs the destination and lands in the nightmare cutscene
  in scene 81 with no text shown. Watched to the same point (scene 81, frame 459/460), the save
  context differs only in the counter at +0x15B4. Same on standalone Azahar in real time.
- Still to prove: a cutscene with a choice, and MM3D.

## Proof
On real cutscenes in each game, reached through a warp from a copy of the save: one run watches to
the end and one presses Start. Compare the save context (the flag arrays, scene, entrance, time) read
over Azahar's RPC after both land, and look at the frames.
