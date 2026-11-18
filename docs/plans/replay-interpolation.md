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
  `Gameplay_Draw`, which records the tick's draws without SVCs; `Graph_DrawFrame` after it on the
  game thread turns them and the HUD into the PICA list and the bound command list's requests
  (four lists and the two screens' display transfers); its last call, the frame end, waits the
  vsyncs and hands the requests to the GSP. A tick's frame shows after the next tick's draw.
- MM3D: not traced yet; the same engine shape is expected (`Play_Update` is named).

## Design

1. **The draw section.** OoT3D: from `Gameplay_Update`'s branch to `Gameplay_Draw` (+0xA8) to
   `Graph_DrawFrame`'s call of the frame end (+0xD40), not run (`patch/oot3d/interpolation.toml`).
   Its output is the bound command list's requests and the PICA lists they name
   (`app/src/gx_cmdlist`). It reaches no device; its only SVCs are ArbitrateAddress signals,
   which a replay answers without the kernel.
2. **Running it in Azahar.** The core gets a replay owner beside the CPU: after the real tick's
   draw returns and before the frame end runs the list, it saves the ARM11 context, writes the
   blended tracks, calls the section on the game's thread with interrupts and guest time held,
   takes the list it wrote, and restores. An SVC, a GSP or HW register access inside the replay
   refuses that in-between.
3. **The sandbox.** Every guest store during a replay is journalled and undone. Azahar's dynarmic
   has no fastmem arena: it reads a page table of host pointers, and a null entry sends the access
   to the slow path. The store journal (`core/store_journal`) nulls the pointers of the process's
   writable pages; the first store to each saves the page and restores its pointer, so later
   stores run at full speed. Cost per OoT3D tick: about 65 page copies and 8.7k loads that pass
   the slow path before their page is first stored to; the draw runs in about 300 us of host time
   with the journal armed, a replay with its rollback in about 480 us. The other candidate,
   write-protecting host pages and journalling on the fault, was not built: it would also journal
   and roll back the HLE services, DSP and GPU stores into the same memory, which a replay must
   leave alone, and the core has no fault handler to host it.
4. **The GPU.** `GPU::RunDetached` runs the replay's requests: its lists through the same PICA
   command processor with interrupts muted and the PICA state saved and put back around them,
   and each display transfer to a screen's display buffer into that screen's in-between image
   (`RasterizerCache::DisplayTransferInto`), not guest memory. The display buffers the game
   swaps are not touched. The lists draw into the game's own render target surfaces, which the
   real lists draw over next; a render target of the replay's own is not built.
5. **Pacing.** Each tick's frame shows on two vblanks. The in-between made from tick k's section
   goes to the display buffer tick k will show in; the renderer shows it on the second vblank of
   tick k - 1's frame and drops it when tick k's frame shows (`RasterizerVulkan::TakeInBetween`),
   so the presents run k - 1, in-between, k at 60 Hz. Higher refresh is not built.
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

- Step 1 (OoT3D census): done, decomp/oot3d `docs/frame.md` "What the draw section touches".
- Step 2: the store journal and the replay owner (`core/section_replay`) are in the fork; the app's
  `interp census|probe|on|report` (docs/app.md) drives them. From one savestate, a 6 s walk with a
  replay every tick ends with the RNG seed, `PlayState` and the player actor byte for byte equal
  to the walk without (measured when the section ended at `Gameplay_Draw`'s return).
- Step 3 (OoT3D): done. The section runs to the frame end call. At t = 1 the replay's requests
  and PICA lists equal the real ones byte for byte in 180 of 180 ticks of a Kokiri Forest walk
  (35.9 MB of lists, run `interp-probe-kf`). Its memory differs from the real run's in three
  places: the DSP parameter counter at 0x54AB60 (written by SendParameterToDsp about 160 times a
  second, the sound thread's), the pool head at 0x5BE20C (frame.md) and the page at 0x15A0A3C8
  (FUN_00407518, about 200 writes a second). The last two are put down to other threads by their
  write rate, not proven; the comparison is per page, not per byte. With `interp on` in Kokiri Forest with the camera turning
  (C-stick, free camera), every present differs from the one before, and the in-betweens sit
  halfway: the far wall moves 3.5, 3.5, 5.5, 5.5 pixels per present where interpolation off
  moves 0, 10.5 (scratch/runs/interp-kf-4 `on/`, `off1/`, crops `on-crops-6-10.png` and
  `off-crops-6-10.png`). The logic rate is unchanged: `PlayState.state.frames` advances 120 in
  4 s off and 120 in 4 s on. Cost in Kokiri Forest: a replay is about 0.9M instructions and
  about 2 ms of host time with the list run. Actors and skeletons still move at 30 Hz, and the
  replay draws into the game's own render target surfaces (Design 4).
  (2026-11-18)
- Step 4 (OoT3D), actors: built (`app/src/actor_track`). Each live actor in the 12 actor lists
  pairs by address and id across two ticks and its `world.pos` and `shape.rot`, which
  `Actor_Draw` builds the model matrix from (frame.md "Actors in the draw"), are blended; an
  unpaired actor draws as it is. In Kokiri Forest about 95 actors pair a tick, none unpaired.
  Link turning from facing right to facing the camera shows a three-quarter view on the
  in-between where interpolation off jumps straight across (scratch/runs/interp-actors-2,
  `turn-zoom-0-4.png` against `off-zoom-0-4.png`).
- Step 4, skeletons: built (`app/src/skeleton_track`). The core records the real section's calls
  through `SkelAnime_DrawOpa`'s queue call (`SectionReplay::Observe`); each skeleton drawn in
  both ticks, paired by address, model, joint table and limb count, has its joints blended as a
  unit (rotation slerped, translation straight). About 6 skeletons a tick in Kokiri Forest, none
  unpaired. Link walking: every present has its own leg pose, where interpolation off repeats each
  pose twice (scratch/runs/interp-skel-1, `walk-zoom-20-25.png` against `off-zoom-20-25.png`).
- Step 4, cuts: a play state started again (`GameState.frames` not one on from the previous
  tick's: a scene change, warp or respawn) holds the interval, as do another play state or
  camera. A warp from Kokiri Forest to Link's house cut once and fades in cleanly
  (scratch/runs/interp-cut-1, `cut-112-117.png`). Not found yet in this build: door cuts
  (Shipwright's `Camera_Special9` timer) and cutscene shot changes (`Camera_SetCSParams`).
- Step 4, fades: built (`app/src/fade_track`). The scene's fade-in is the transition's screen
  fill (decomp/oot3d `docs/frame.md` "Fades"): while `transitionCtx.draw` is
  `TransitionFade_Draw`, its fill colour is blended, and the fade flash's always; a fade pairs by
  address and type. Warping into Link's house, the room brightens on every present (mean
  luminance 5.0, 7.7, 10.4, 12.8 ... 46.6, 49.3, 53.6) where interpolation off holds each step
  for two (scratch/runs/interp-fade-on `fade-54-75.png`). The first step out of black has no
  in-between: the play state's frame count is not yet consecutive there, which cuts.
- Step 4, material animations: built (`app/src/material_track`). The scene's texture scroll is
  stepped in the draw (decomp/oot3d `docs/frame.md` "Material animations"): the core observes
  `Scene_Draw`'s calls of `AnimPlayer_Update`, and each animation stepped in both ticks with the
  newer frame one step on from the older one (wrapped or held as the game does) starts its step
  `t` of the way on, so the draw's own step lands halfway. Kokiri Forest's waterfall flows on every
  present (about 10 changed pixels a present in its crop) where interpolation off moves on every
  second (55 to 72, then 0) (scratch/runs/interp-mat-1 `on-waterfall-10-17.png` against
  `off-waterfall-10-17.png`). Not covered: the time of day's list (its frame is set from the time)
  and animations actors step in their own update or draw.
- Step 4, effect pools: built (`app/src/effect_pool_track`). Kokiri Forest's floating motes are
  `ObjectKankyo`'s fairies (decomp/oot3d `docs/frame.md` "Effects"): in each ObjectKankyo with the
  fairies' params that the actor track pairs, each effect in use with its `state` and `base`
  unchanged has `pos` blended. About 64 pair a tick, a few unpaired (respawned). A mote moves on
  every present (scratch/runs/interp-mote-on `on-mote-12-19.png`); with interpolation off the
  picture is unchanged on every second present (273, 0, 273, 0 changed pixels).
- What still moves at 30 Hz in Link's house and Kokiri Forest standing still: the fairies'
  twinkle (`alpha`, stepped by the draw by conditions on `timer`, not blended) and Navi's
  sparkles (not traced). Not built: actors' own material animations and the time of day's list,
  the remaining declared cuts, the replay's own render target, MM3D and the option. (2026-11-18)
- Step 4, declared cuts. Shipwright declares two camera cuts: `Camera_Special9` when the door
  timer runs out and `animState` goes 1 to 2 (the eye jumps to the door's far side), and
  `Camera_SetCSParams`. Door cut: built. A door within a scene (the Fire Temple lobby's left door,
  savestate `fire` in the harness states: `teleport:-280,0,300 walk:-280,175,3 a`) runs setting
  0x1D, function 0x46 `Camera_Special9`; `animState` holds 1 for 21 ticks, then becomes 2 in the
  tick that moves the eye beyond the door (decomp/oot3d `docs/camera.md` "Fields past the view").
  `camera_track` picks the active camera's function as `Camera_Update` does (`sCameraSettings` by
  setting and mode) and cuts on the tick that enters `Camera_Special9`'s far-side state. Opening
  the door with interpolation on, the present before the jump repeats the last tick (0 changed
  pixels) and the next shows the far side whole; the presents after it blend again
  (scratch/runs/door-cut-1 `cut-112-119.png`). Two in-betweens there are refused (svc 0x22 at
  0x335248, the room behind the door loading), as the sandbox should. The back alley's doors load
  a scene under setting 0x21 and are cut by the play state's restart.
  Cutscene shot cut: built. `Camera_SetCSParams` is 0x2C4130, called by the N64 point-list
  commands, and Grezzo's spline command 0x97 picks the segment covering the frame into
  `csCtx.camShot` (decomp/oot3d `docs/cutscene.md` "The camera's shots"); a shot starts exactly
  when `camShot` or a point list's applied frame changes, and `camera_track` cuts there. In the
  Kakariko intro (savestate `kak`, saved two seconds after warp 0xDB from `kf`), the present
  before the change from the village to the graveyard gate repeats the last tick (0 changed
  pixels) and the next shows the new shot whole (scratch/runs/shot-cut-2 `shot-386-393.png`);
  the cutscene's start and end are cut by the active camera changing (`shot-546-553.png`).
- The first step out of black after a scene load: not a cut. Warping into Link's house, the old
  scene's last section is frame 215 and the new scene's first closed section is frame 12 (frames
  1 to 11 close no section while the scene loads), so (215, 12) is cut by the restart and (12, 13)
  is blended: no refusal, the fade paired. Yet the present before tick 13's frame is black
  (mean luminance 0.0, then 5.0, 7.7, 10.4, scratch/runs/fade-5): the in-between is made but never
  shown. `RasterizerVulkan::TakeInBetween` shows it only on a later vblank of tick 12's frame,
  and is skipped while an LCD colour fill is on; which of the two holds there (tick 12's frame on
  one vblank after the load's lag, or the screen still filled) is not measured yet. Next: count
  per vblank whether the in-between is due, the shown buffer and the fill state across this load.
