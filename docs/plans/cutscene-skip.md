# Press-Start cutscene skip (OoT3D and MM3D)

User: "a cutscene skip mod (by pressing start), I don't think there is a skip mod but we can make one".

## What it does
During a skippable cutscene, Start ends it: the game lands in the state it would reach by watching
to the end (flags, items, time of day, entrance, music), with no other change.

## Design
- Our own MIT `code.ips`, written from scratch, linked with clang/lld (`armv6k`, `mpcore`) against
  the addresses of the code it runs on (vanilla for OoT3D, Project Restoration's v1.0 layout for
  MM3D). It rides on the set's baked `code.bin` (see `docs/mods.md`, "How Azahar
  2126.2 stacks mods"), so it never links GPL or third-party code.
- Each hook in `hooks.s` carries the original instruction in a matching `.expect.<name>` section;
  the installer refuses to install unless the baked code holds exactly that word at the hook.
  Our payload goes after the base payload in `.bss`, and the installer grows `.bss` in the base
  exheader to cover it.
- One press of Start ends the cutscene within the frame it is pressed: the player never sees the
  rest. Inside that frame the patch runs the game's own cutscene interpreter for every remaining
  frame up to the script's end frame, so every command the script would have issued (destination,
  flags, time of day, music, the stop command) runs in order, and the game ends the cutscene itself.
  It sets no flags of its own. User: "don't fast forward please, skip entirely".
- Text: a text command holds its end frame while the textbox is open. On a hold the skip closes
  the box once, stops stepping for that game frame, and resumes once the game has finished closing
  it; closing again every frame stalls the cutscene. A choice is never closed: the skip
  waits there for the player, then closes the answer's own text. A choice box only reports itself
  once it has printed, so the skip reads the held text entry from the script: its type on OoT3D
  (`patch/oot3d/cutscene_script.hpp`), its alternate texts on MM3D, whose scripts mark a question
  only by what follows an answer (`patch/mm3d/cutscene_script.hpp`).
- Transitions: the skip refuses to start, and stops stepping, while a scene transition is requested.
- `patch/common/cutscene_skip.hpp` is the skip; each game's `cutscene_skip.cpp` supplies only its
  layout as a `Game` trait. The functions, globals and offsets it uses are generated from the game's
  decomp (`patch/<game>/symbols.toml`, `tools/z3dmods/symbols.py`); the RE behind them is in
  `decomp/oot3d/docs/cutscene.md` and `decomp/mm3d/docs/cutscene.md`.
- OoT3D: `patch/oot3d/` replaces the interpreter at its two call sites in
  `CutsceneHandler_RunScript`. Start counts in normal play, after the first second of the cutscene
  (the game's own title-demo minimum), with no transition pending.
- MM3D: `patch/mm3d/` replaces the interpreter at its three call sites (`Cutscene_BeginScript`,
  `CutsceneHandler_StartScript`, `CutsceneHandler_RunScript`). The title screen's demo is a
  play-state cutscene, so the skip requires `CommonData.setup` to be 0.

## Status
- OoT3D Kakariko intro (warp 0xDB from Link's house, misc stop at frame 255): Start at frame 57 ends
  it the same frame; the landing frame matches watching it to the end, and the save context differs
  only in `naviTimer` (+0x4C) and the counter at +0x15B4.
- OoT3D opening narration (File 1 copy, `poke:0x588EF8,f1ff warp:0xBB`: nextCutsceneIndex 0xFFF1
  into Link's house, as a new file does; script 0x0877CE84, end 4500): Start at frame 74, with the
  first box on screen, closes each box, runs the destination and lands in the nightmare cutscene
  in scene 81 with no text shown. Watched to the same point (scene 81, frame 459/460), the save
  context differs only in the counter at +0x15B4. Same on standalone Azahar in real time.
  From a file made in the same run (`oot-nf2`: File 2 named, "Save completed", A on the list's
  File 2, A on Start): the same script at frame 74 after 10 s, and Start lands in scene 81.
- MM3D's title needs no skip of ours (`mm-q-title`, `mm-q-title2`, `mm-q-title3`, 2026-11-14). From
  boot the game plays its intro (`v_l_mujyura.jdb`, Majora's Mask in the dark) and then, with no
  input, demo scenes of Clock Town (`v_l_clocktower.jdb` and others) without the logo. Start
  during either shows the title at once, the logo and "PRESS START" over the scene that is
  playing (frame 119 of the intro, frame 580 of the demo), and Start again opens file select.
  The game's own Start already ends the demo to the title cleanly, so the patch leaves it alone
  (`CommonData.setup` keeps our skip out of these play-state cutscenes).
- MM3D, new file from boot in the harness: Start on the title demo (frame 650) still opens file
  select and the demo is not skipped. Start at frame 119 of the opening narration (`legend.jdb`)
  closes each box and lands in the Lost Woods script `lw_meetstk.jdb` (scene 101), where watching
  it also leads. Start there at frame 296 ends it and the game runs on into `losthorse.jdb`.
- MM3D against a watched run (A pressed through the text, `waitscript`), compared at frame 100 of
  the next script: the narration and the woods cutscene each leave CommonData's `start`, `scene`
  and `SaveData` (0x1A90 bytes) byte-identical to watching, and the frames match.
- MM3D on standalone Azahar in real time (File 1 copied from a harness run into the player's save
  folder for the run, then removed): Start at frame 107 of the narration runs it to its end (780)
  and the transition within a second; Start in the woods at 118 goes straight to `losthorse.jdb`.
- OoT3D Deku Tree after Gohma (`poke:0x588EF8,f1ff warp:0xEE`, script end 4500): Start at frame
  60 runs to the choice at 499 and waits there; Yes or No (D-pad) lands in the next script in scene
  71 as watching does. At its frame 100 the save context differs from watching only in +0x15B4.
- MM3D milk run raid (`climia_raiders.jdb`, the game's one script that asks; reached from a new
  file with `poke:0x77643E,01 poke:0x776447,80` and day 2, 18:45 poked into the save, then
  `warp:0xCE20`): Start at frame 100 runs to "Do you understand?" at 662 and
  waits. Yes ends the cutscene at 750 in the arrow minigame; No closes Cremia's reply and asks
  again, as watching does after her explanation. At the cutscene's end the save differs from
  watching only where two watched runs differ too (+0x1380..0x1393 and +0x1A88).

## Proof
On real cutscenes in each game, reached through a warp from a copy of the save: one run watches to
the end and one presses Start. Compare the save context (the flag arrays, scene, entrance, time) read
over Azahar's RPC after both land, and look at the frames.
