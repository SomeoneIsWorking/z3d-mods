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
- MM3D: decomp/mm3d `docs/frame.md`. `Play_Main` runs `Play_Update`, then `Play_Draw` (at
  +0x160); the game thread's `Graph_DrawFrame` ends with a direct call of the frame end (+0xA04),
  which runs the bound command list. The engine's shape is OoT3D's.

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
   fills and transfers through the rasterizer cache only (one it cannot run is skipped and
   counted, never run in software), and each display transfer to a screen's display buffer into
   that screen's in-between image (`RasterizerCache::DisplayTransferInto`), not guest memory.
   `SectionReplay::Replay` runs inside the cache's overlay (`RasterizerCache::OpenOverlay` to
   `CloseOverlay`, around the exchanges, the replay and its rollback): the first image write to a
   surface that existed at the open (a draw's colour or depth target, a validation, a copy or
   blit, a texture cube's faces) copies it into a new slot and swaps the copy into its place, so
   the replay writes the copy; a kept surface the replay unregisters keeps its slot. Closing
   retires every surface made meanwhile, swaps the originals back (the copies are sentenced, so
   the GPU is done with them before they go), registers again what was unregistered, and puts back
   each kept surface's metadata, the dirty regions and the texture cubes. GPU writes to guest
   memory meanwhile (a flush's download) are saved in the store journal first
   (`MemorySystem::JournalPhysicalStore`) and rolled back with the replay's stores.
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
6. The option in each game's options ("Frame interpolation"), on by default for a game once it passes.

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
  about 2 ms of host time with the list run. (2026-11-18)
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
  the remaining declared cuts, MM3D and the option. (2026-11-18)
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
- The first step out of black after a scene load: a declared cut, not a presenter defect.
  Warping from Kokiri Forest into Link's house (`warp:0xBB`), logged per vblank: no LCD colour
  fill is on at any vblank; the new scene's first closed section (frame 12) is cut by the play
  state's restart, and tick 12's frame shows on the vblank after the loading screen's last black
  one. The in-between of (12, 13) is made in tick 13's section and shown on the second vblank of
  tick 12's frame. Presents with interpolation on run 0.0, 5.0, 7.7, 10.4, 12.8; off, 0.0, 5.0,
  5.0, 10.4, 10.4 (scratch/runs/black-1 `fade-113-117.png`, black-off): tick 12 shows at the same
  present in both, so the earlier reading (fade-5) that the present before tick 13 was black was
  one present off. The black present is the old scene's last frame, which no in-between may blend
  across.
- The replay's effect on the game's GPU state: fixed (fork 230d77c83). `interp leakcheck`
  (docs/app.md) compares the rasterizer cache's state, surface contents read back, and FCRAM and
  VRAM before and after each replay. Before the overlay, every replay of a Kokiri Forest walk
  leaked (119 of 119, scratch/runs/leak-kf-2): the top and bottom screens' colour targets changed
  contents, textures sharing a page with the section's stores were invalidated or unregistered by
  the journal's page-wide invalidations, and surfaces the replay made stayed registered. The
  RequestDma copies (45 in the Fire Temple door run) were journalled already (`CopyBlock` writes
  through `WriteBlock`); their cache invalidations were the leak. With the overlay: a Kokiri
  Forest walk with the camera turning, 179 replays, 0 leaking (scratch/runs/leakcheck-kf); the
  Fire Temple door run, 196 replays (2 refused, 2 cuts), 0 leaking (scratch/runs/leakcheck-door).
  The presents of both runs with `interp on` are byte for byte those of the build before
  (overlay-kf-new against overlay-kf-head, 180 of 180; overlay-door-new against
  overlay-door-head, 246 of 246), the t = 1 probe still gives 180 of 180 identical lists, and its
  memory differences are the build before's (identical=10, different_pages=286). The overlay
  costs about 0.55 ms of host time a replay in Kokiri Forest (2.61 against 2.06 ms, one run each,
  not a perf-units measurement). Depth surfaces are compared by the 4 bytes a pixel their download
  writes (`DepthToBuffer` packs D24S8), not the 5 the staging buffer holds. Not covered by the
  check: a custom texture's asynchronous decode (play loads them asynchronously; in a replay
  only an already decoded one is uploaded, otherwise the guest's image shows in that in-between).
  Harness states load only in the core revision that saved them; their recipe is in docs/app.md.
- Pacing (Design 5): measured in real time with `pace on` and `pacing` (docs/app.md), which read
  the core's own counts: emulated vblanks, and the game's frame submissions, one a logic tick. A
  Kokiri Forest walk with the camera turning, 10.03 s each, on this host at load 2.6 to 2.9:
  interpolation on 59.83 vblanks and 29.914 ticks a second (0.500 a vblank, real time 1.000 of
  emulated), off 59.83 and 29.914 (scratch/runs/paced-on, paced-off; two runs each, the same to
  three decimals). A first run at load 3.5 ran on at 0.987 of real time and off at 1.000, so a
  busier host costs the replays their margin. The harness presents to no display, so this is
  the emulator's pacing, not a display's flips; a windowed run with the option (below) is the
  one that shows flips.
- Step 6, OoT3D: the option. "Frame interpolation" (`frame_interpolation` in
  `patch/oot3d/options.toml`, an app switch, on by default) is a row of the game's options on the
  picker (X on its tile); `Player` turns `FrameInterpolation` on with it while the game plays,
  from a direct launch or the picker, and logs `pacing` every 10 s with the window's presents
  counted by the core (`PerfStats::AddWindowPresent`, and those that showed an in-between). The
  windowed app in Gamescope (`z3dmods inputtest interp`, Link's house, mouse turning the camera):
  59.83 flips a second, 29.96 and 29.91 of them in-betweens, 29.86 and 29.91 ticks, 0.499 and
  0.500 ticks a flip, real time 1.000 of emulated (scratch/input-test/interp `pacing.txt`); with
  the option off (`interp-off`) 59.83 flips, none an in-between, 29.91 ticks. The title screen
  and file select make no in-betweens (no play state). `options-oot3d` shows the row on and the
  game booted from the picker logging `frame interpolation on`. The windowed counts show which
  presents are in-betweens; that each sits halfway is shown by the harness crops above, through
  the same presenter (`RendererVulkan::PrepareRendertarget`).
- Step 5 (MM3D): the draw section is found by disassembly, from `Play_Main` +0x160
  (`PLAY_MAIN_DRAW_CALL`) to `Graph_DrawFrame` +0xA04 (`GRAPH_DRAW_FRAME_END_CALL`), neither site
  touched by our patch. `FrameInterpolation` runs each track only when the game's
  `interpolation.toml` names it (its first key; then all of its keys are required), and refuses a
  table that blends without the camera track's cuts; `patch/mm3d/interpolation.toml` names the
  section and the command list (`gNngxState` 0x7B2700, the same object as OoT3D's) and no track.
  Census (decomp/mm3d `docs/frame.md` "What the draw section touches", run mm-interp-census): about
  195 pages a tick, 23 command lists, 2 DMAs, a fill and the two display transfers, and on the
  section's thread three gsp::Gpu FlushDataCache requests and two 100 us SleepThreads (a vertex
  upload that sleeps after itself). The replay answers both itself (fork 82a68448a): the GSP's
  FlushDataCache is a stub that only replies, and a sleep in a replay has no thread to yield to.
  t = 1 probe in South Clock Town (mm-interp-probe): 180 of 180 replays done, requests and PICA
  lists byte for byte the real ones in 180 of 180 (5.66 MB); memory identical in 91, the rest only
  in two pages other threads write meanwhile. `interp leakcheck` (mm-interp-leak, walking and
  turning the camera): 180 replays, 0 refused, 0 leaking, 360 in-betweens, 0 GPU skipped. With
  `interp on` and off from the same state and input, the player actor (0x800 bytes), the first
  0x400 bytes of the play state and of `gCommonData` are byte for byte equal after 6 s
  (mm-interp-logic-on, -off). The harness state `sct` (MM3D, this core): a new file through
  `MM3D_BOOT` (tools/z3dmods/input_test.py) then `savestate:sct`. Next: the tracks' fields in
  decomp/mm3d, the camera first (with its cuts), then actors, skeletons, fades, materials and
  effect pools.
