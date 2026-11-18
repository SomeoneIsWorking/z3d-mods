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
   (`app/src/gx_cmdlist`), each request turned into the GSP commands nngx's dispatcher sends for
   it (decomp/oot3d `docs/frame.md` "Requests"). It reaches no device; its only SVCs are ArbitrateAddress signals,
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
- The fairies' twinkle: blended. Their draw (0x159B2C) steps each `alpha` once a draw, so the
  track blends it from the two samples taken before the real draws and the replay's draw steps
  the blend once, which lands it the fraction of the way to the next tick's. A fading mote from
  `loadstate:kf` (scratch/runs/kf-fairy2 against kf-fairy-before, the build without the blend):
  its peak brightness falls on every present (211 203 192 179 177 170 162 153 144 132 120) where
  before it repeated in pairs (211 210, 153 153, 134 132); `mote-after-vs-before-10-21.png`,
  after above. The real presents equal interpolation off's (kf-fairy2 on and off, mean
  difference 0.0). Still at 30 Hz in Link's house and Kokiri Forest standing still: Navi's
  sparkles (not traced). Not built: actors' own material animations and the time of day's list,
  the remaining declared cuts, MM3D and the option. (2026-11-18)
- OoT3D step maps (harness states `kak2`, Kakariko at night after its intro; `field`, Hyrule
  Field at night by two Stalchildren, warp 0xCD from `kf`; `deku`, inside the Deku Tree, warp 0x0;
  scratch/runs/<state>-step `step0.png`/`step1.png`, 6 s each, 0 refused): Kakariko and the Deku
  Tree step nowhere; Hyrule Field stepped in one place (424 pixels, `field-step/step1.png`), the
  Stalchildren moving into every present. The actors' own material animations and the time of
  day's list showed nothing stepping in these three states.
- OoT3D's target arrow: interpolated. What stepped in `field` was not Navi's sparkles: crops of
  the presents show the yellow Z-target arrow spinning over a Stalchild, its shape changing every
  second present. The game's soft sprites (the `EffectSs` pool at 0x598B98, records 0x88 bytes,
  type at +0x63, life at +0x60) held only debris falling below the field and a type 0x15 there,
  and the scan for Navi's drawn position found her glow's billboard batch, three points along her
  velocity, rebuilt each draw. `Target_Draw` (0x47B124) turns the arrow by
  `PlayState.gameplayFrames` times 3000 as MM3D's does (decomp/oot3d `docs/frame.md` "The target
  arrow"); the patch hooks the same two instructions (`patch/oot3d/target_arrow.cpp`, sharing
  `patch/common/target_arrow.hpp` with MM3D) and turns it by the link block's `draw_fraction`
  of a step. With interpolation on the arrow narrows on every present where off holds each turn
  for two (scratch/runs/arrow-on `arrow-off-vs-on-160-167.png`, off above), and the field's step
  map loses it (scratch/runs/arrow-leak `step1.png`, 50 pixels left: Link's red damage flash at
  his feet, next). `interp leakcheck` there: 179 replays, 0 refused, 0 leaking. MM3D after the
  change: South Clock Town's step map shows only the clock's minute digit (mm-arrow-common).
- OoT3D's damage flash: blended. Link's red flash after a Stalchild's hit stepped in `field`:
  `Player_Draw` adds 50 less `invincibilityTimer` (8 to 40) to `Player.damageFlashTurn` itself
  each draw (decomp/oot3d `docs/frame.md` "Actors in the draw"), so a replay drew the newer
  tick's flash. The actor track's draw phase (`actor_draw_phase` in the table, for the player's
  id) blends the turn from the two samples taken before the real draws the short way round 0x100,
  and the replay's draw steps it once. His boots darken on every present with interpolation on
  where off holds each shade for two (scratch/runs/flash-leak `flash-off-vs-on-200-207.png`, off
  above); the step map keeps 3 pixels of the arrow's edge (`step1.png`). `interp leakcheck`:
  179 replays, 0 refused, 0 leaking, 16826 actors paired, none unpaired.
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
  check: a custom texture's asynchronous decode. A replay decides nothing about custom textures
  (fork `RasterizerCache::UploadCustomSurface` and `ValidateSurface` under the overlay): it starts
  no decode and uploads nothing over a surface, so it draws a custom image only where the real
  frame bound one and the guest's image where the real frame did. The window's asynchronous
  loading after File 1 loads (`z3dmods inputtest interp-textures` and `interp-textures-mm3d`,
  which record every present): each in-between's textures are its neighbours'
  (scratch/input-test/interp-textures `entry-1653-1658.png`, Link's house as it fades in, and
  interp-textures-mm3d `entry-3210-3215.png`, the clock tower base, real frames and in-betweens
  in turn). The build before showed no in-between with the guest's image on this host either,
  so this proves the rule, not a fix of a seen frame.
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
  `MM3D_BOOT` (tools/z3dmods/input_test.py) then `savestate:sct`.
- Step 5, MM3D camera: built. The camera track's door and shot cuts are their own groups of the
  table, each used where a game names it; MM3D names the view (`PlayState.view` +0x1F8, the N64
  layout, written by `View_LookAt` 0x184618), the cameras (+0xA68, the active index +0xA78) and
  the frame count (+0x138), decomp/mm3d `docs/camera.md` "The view". Turning the camera in South
  Clock Town's clock tower base (mm-cam-on, mm-cam-off): the wall moves 3.5 to 4 px on every
  present where interpolation off moves 0, 7.5, 0, 7.5 (`on-vs-off-33-38.png`, on above, off
  below). The logic rate is unchanged: the frame count advances 60 in 2 s with interpolation on.
  A warp (mm-cam-cut) cuts once at the restart; two in-betweens of the load are refused (an
  ArbitrateAddress wait). The scene's white fade out and fade in still step on every second
  present (no fade track yet). OoT3D after the change: the Fire Temple door run still gives 2
  cuts and 2 refusals in 136 replays. Not found yet in MM3D: the door and cutscene shot cuts.
- Step 5, MM3D actors: built. `Actor.shape.rot` is +0xC0 (decomp/mm3d `docs/actors.md` "The
  model's place in the draw"); about 40 actors pair a tick in South Clock Town's tower base, none
  unpaired. Link turning while he runs: with interpolation on every present has its own heading
  where off repeats each one twice (scratch/runs/mm-act-on `on-vs-off-link-36-47.png`, on above).
  His leg pose still steps at the tick rate (no skeleton track yet).
  Next: skeletons, fades, materials, effect pools, then the door and shot cuts; then the option.
- Step 5, MM3D skeletons: built. MM3D's `SkelAnime_DrawOpa` (0x1FEB14) and `SkelAnime_DrawOpaEx`
  (0x3780B0) queue a skeleton through one function, each with the `SkelAnime` in r1; the table
  names both calls (`skeleton_draw_calls`, a site list in both games now). The draw reads the
  joint table at `SkelAnime` +0x80 limb by limb through the actor's limb controller, after the
  update posed it (decomp/mm3d `docs/actors.md` "Skeletons"). Three skeletons pair a tick in South
  Clock Town's tower base, none unpaired. Link running toward the camera: every present has its
  own leg pose and the in-betweens sit halfway (his tunic's centroid 142.25, 140.86, 139.69,
  137.83, 134.98), where interpolation off repeats each pose twice (scratch/runs/mm-skel-on
  `on-vs-off-link-36-40.png`, on above). OoT3D after the change: Kokiri Forest walking, 318
  skeletons paired, poses between ticks (scratch/runs/oot-skel-kf `walk-vs-off-36-40.png`); the
  Fire Temple door run, 2 cuts and 2 refusals in 166 replays.
- Step 5, MM3D fades: built. A scene's fade is OoT3D's `TransitionFade` (decomp/mm3d
  `docs/frame.md` "Fades"): the transition at `PlayState` +0xC860 with its draw at +0xCA94, and
  the fade flash at +0xCAAC; the table names them and the fade track runs unchanged. A warp to
  South Clock Town (mm-fade): the white fade out brightens on every present (mean luminance 44.0,
  47.1, 50.3, 54.2, 57.5 ...) where interpolation off holds each step for two (44.0, 44.0, 50.3,
  50.3, 57.5), and the fade in from black after the load the same (7.2, 8.6, 9.6, 11.0 against
  7.2, 7.2, 9.6, 9.6); the first step out of black is cut by the play state's restart, as in
  OoT3D (`fade-out-3-8.png`, on above). 114 replays, 2 cuts, 2 refused (the load).
- Step 5, MM3D material animations: built (fork 548c1a4e1). MM3D's `AnimPlayer` is a C++ class
  (decomp/mm3d `docs/frame.md` "Material animations") and the scene's are stepped and applied in
  the update (`Scene_UpdateRoomAnims` from `Play_Update`), so the draw only reads the materials
  they wrote. The core's replay therefore makes lead calls: `SectionReplay::ObserveLead` records
  the calls the section's thread makes through a site before the section, and `Replay` makes the
  chosen ones again from the section's starting state, each returning to the open site, before
  the section runs with its own registers. The table names `AnimPlayerList_Update`'s call of each
  player's `AnimPlayer_Update` (`material_lead_calls`); `material_track` writes a paired player's
  frame a step short of the in-between's and makes its call again, so the game's own step, wrap
  and apply land halfway. Found by a memory scan for a float stepping by the word after it, then
  write watches on it and on the game thread's stack for the pushed return addresses. In the
  Laundry Pool (harness state `pool`: `loadstate:sct warp:0xDA00 wait:8 savestate:laundry
  stick:0,-30000,2.5 wait:1.5 savestate:pool`) the stream's water changes on every present, by
  about half a tick's change (mean 0.5 against 0.9 to 1.1 a tick in its crop), where
  interpolation off changes it on every second present (scratch/runs/mm-pool-on3
  `water-on-vs-off-10-13.png`, each crop over its change from the present before, x8; on above).
  One player pairs a tick, none unpaired; `interp leakcheck` there, walking: 119 replays, 0
  refused, 0 leaking; the play state's first 0x400 bytes, the player and the animation player
  are byte for byte equal after 4 s with interpolation on and off. OoT3D after the change: Kokiri
  Forest's waterfall still flows on every present (scratch/runs/oot-mat-kf
  `waterfall-on-vs-off-10-15.png`), the Fire Temple door run gives 2 cuts and 2 refusals in 166
  replays. Not covered: actors' own material animations (`AnimPlayer_Update` reached through
  0x224148, a model's update).
- MM3D, where the work stands (2026-11-18).
  - The option: built. "Frame interpolation" (`frame_interpolation` in
    `patch/mm3d/options.toml`, on by default), as OoT3D's. The windowed app in Gamescope
    (`z3dmods inputtest interp-mm3d`: File 1 at South Clock Town's owl statue, the mouse turning
    the camera): 59.83 flips a second, 29.86 and 29.91 of them in-betweens, 29.96 and 29.91
    ticks, 0.501 and 0.500 ticks a flip, real time 1.000 of emulated (scratch/input-test/
    interp-mm3d `pacing.txt`, `02-turned.png` in play).
  - The target arrow: built (option a). The "spinning green rupee" over the owl statue is the
    Z-target arrow (decomp/mm3d `docs/actors.md` "The target arrow"): `Target_Draw` turns it by
    `gameplayFrames * 3000`, an integer stepped once a tick. The link block (`slot_link.py`) has
    one draw frame fraction, `draw_fraction`, which `FrameInterpolation` writes as a replay input
    of t - 1 (-0.5: the replay draws the newer tick) and which is 0 in every real draw; the
    patch (`patch/mm3d/target_arrow.cpp`, hooked at `TARGET_DRAW_ARROW_TURN_MOVE` and
    `_CONVERT`) turns the arrow by the angle plus the fraction times `TARGET_ARROW_TURN_STEP`.
    Other draws that step on `gameplayFrames` take the same fraction once seen stepping, each
    named in the decomp. In the tower base (`loadstate:sct patch:off patch:on`: a state holds the
    code it was saved with), the arrow's box changes on every other present with interpolation
    off and on every present with it on, each in-between's turn between its neighbours'
    (scratch/runs/mm-arrow-on `arrow-off-vs-on.png`, top off, bottom on). Link's edges move
    with him: of the change in his box, 48% falls into the in-betweens with interpolation on and
    0% off (mm-arrow-on, mm-arrow-off); single pixels that flag at one parity are his outline's
    aliasing.
  - Effect pools: not found. South Clock Town at 6:10 on the first day (mm-sct-fx-2,
    `stepmap.png`: pixels that change into a real tick and not into the in-between before it)
    shows no particles stepping: the clock's minute digit, the dog, and noise over Link and the
    plaza's far side that shows at both present parities. Which particles were seen, and where,
    is the next step; a midnight poke of `time` did not take.
  - Door cut: built (the table only; `camera_track` is OoT3D's). A door within a scene runs
    setting 0x8, function 70 `Camera_Special9`, whose step (`Camera.animState`, +0x172) becomes 2
    in the tick that puts the eye beyond the door, as in OoT3D (decomp/mm3d `docs/camera.md` "The
    door camera"). Woodfall Temple's first door (harness state `mayor`, which is Woodfall Temple:
    `teleport:0,-1185,860 wait:1 walk:0,770,4 teleport:0,-1185,745 wait:0.05` then A; walking
    against the door stops 50.6 from it, too far for its prompt): with interpolation on, the
    present before the jump repeats the last tick (0 changed pixels) and the next shows the far
    side whole; the presents after it move again (scratch/runs/mm-door-cut-1 `cut-39-44.png`; 91
    replays, 0 refused, 2 cuts: the first after `interp on` and the door). The Stock Pot Inn's
    doors are all type 5 in their params (bits 7-9), the game's schedule doors, which is
    likely why A did nothing there.
  - Cutscene shot cut: built, as its own cut kind. Script command 90 runs the N64 game's
    `CutsceneCamera_UpdateSplines` on `sCutsceneCameraInfo`, whose eye and at interpolators walk
    the points a group (a shot) at a time; an interpolator stamps a group it has reached with type
    8 and reads it, moving the eye to the new shot, in the next tick (decomp/mm3d
    `docs/cutscene.md` "The camera's shots"). No field changes only as a shot starts, so the table
    names the two group types and the pending type, and `camera_track` cuts in the tick a group
    leaves pending. The Southern Swamp's first visit (`loadstate:sct interp:on warp:0x8400`): at
    both shot changes the present before repeats the last tick (0 changed pixels) and the next
    shows the new shot whole, with the presents after moving again (scratch/runs/mm-shot-cut-1
    `cut-302-307.png`, mm-shot-cut-2 `cut-422-427.png`; 261 replays, 1 refused, 4 cuts: `interp
    on`, the warp, the two shots).
  - The warp's white fade-in is blended: the swamp's mean brightness falls on every present
    with interpolation on (249.75, 246.87, 243.25, 239.57, ...) and in pairs off
    (scratch/runs/swamp-fade `fade-in-124-129.png`). A count of pixels over a threshold read it
    as stepping.
  - Step maps (scratch/runs/mm-steps; a region steps when its change all falls into the real
    ticks): South Clock Town by day, only the clock's minute digit. The Southern Swamp: the far
    water band and Woodfall Temple's speck at (222,12) stepped; both were models' material
    animations, fixed below. Clock Town at night (state `night`, 9:09 on the first night,
    scratch/runs/night-step `step0.png`, 269 replays, 0 refused): only the clock's minute digit.
  - Models' material animations: interpolated. `Model_Update` (0x2240EC) steps the player at the
    model's +0x98 inside the draw section, through its vtable at +0x58 (decomp/mm3d `docs/frame.md`
    "Material animations"), so the replay stepped it once from the start state and drew the newer
    tick's frame. `MaterialTrack` now takes in-section step sites (`material_step_calls`, read from
    the census's calls, frame `from + speed*t`) beside the lead sites, and `material_update`
    keeps only the virtual calls that reach `AnimPlayer_Update`. The swamp's band (state `swampw`,
    rows 96-110) now changes into every present (1.25, 1.27, 1.59, 1.37, ... on; 0, 1.99, 0, 2.37
    off; scratch/runs/swamp-band `band-on-vs-off-41-44.png`), 179 replays leakchecked, 0 leaking.
    Woodfall Temple's speck changes every present on, in pairs off (scratch/runs/wft-step2
    `speck-on-vs-off-44-49.png`), and leaves the step map (`step1.png`); 99 leakchecked, 0
    leaking, 1 refused (svc 0x28 at 0x1DFC04).
  - Pack textures in in-betweens: not reproduced. Windowed `--record` runs with the 4K packs and a
    fast mouse turn while running (`inputtest interp-turn-mm3d`, 5075 presents in South Clock Town;
    `interp-turn`, 3208 in Link's house): no present has a tile under half both neighbours'
    detail except where near geometry crosses the view, and those frames show the pack's textures
    (looked at 2535, 4434, 4704, 4872). Entering several scenes in one recorded run is not covered.
  - The title screen and file select: interpolated. Every replay there was refused (svc 0x22 at
    0x12A150): the section's first entry into `gGspInterruptLock` (0x7C0F50, an nn::os critical
    section) waited for the GSP interrupt thread, which holds it at every title section's start
    (60 of 60: event -1, its TLS the owner) while it runs the previous tick's list, and which a
    replay does not run (decomp/mm3d `docs/frame.md` "The GSP interrupt lock"). `HeldLock`
    (`app/src/held_lock`, the table's `held_lock`) starts every replay, probe included, with the
    lock as its holder leaves it (event 1, no owner, no entries) when a thread other than the
    section's holds it. t = 1 probe from boot on the title (title-probe): 90 of 90 done, requests
    and PICA lists byte for byte the real ones in 90 of 90 (1.93 MB); memory differs only in what
    the interrupt thread stores meanwhile (its time stamp 0x6A3A10, `gNngxState` +0x11 and the
    running list's request index). With `interp on` from a title state (title-ab): Majora's Mask
    over Skull Kid turns on every present where interpolation off repeats each pose twice (73, 0,
    74, 0 changed pixels off, 57, 59, 47, 57 on; `mask-on-vs-off-20-25.png`, on above).
    `interp leakcheck` from boot through Start and A to the clock (title-leak): 374 replays, 0
    refused, 0 leaking, no assert or unknown address in the core log. Play in South Clock Town
    never starts with the lock held.
  - The title-screen crash: fixed. With `interp on`, MM3D crashed a second or two after Start on
    the title screen (`z3dmods run mm3d <dir> --harness interp:on wait:29 start wait:6`; the core
    asserted in `RasterizerCache::UpdatePagesCachedCount` after `Unknown virtual address @
    0x010C8000`). The core dump's stack is a replay that was done, not one refused:
    `GPU::RunDetached`, `AccelerateFill`. `GxCmdlist::read` passed each nngx request to the GPU as
    if it were a GSP command, but a request is nngx's own encoding (decomp/mm3d `docs/frame.md`
    "Requests", read from the dispatcher 0x1490F4 and the same in OoT3D's 0x3027DC): a fill's
    third word is its size and width (0x010C8000: 0xC8000 bytes, 32-bit), not its end, and a
    display transfer's flags are nngx's bits, not the GSP's. It now builds the GSP commands the
    dispatcher sends. The title's play state makes a fill after Start; South Clock Town's fill had
    hit the same path without crashing. Every in-between of both games was made with nngx's
    transfer flags as GSP flags (OoT3D's top screen 0x88, read as a texture copy with no
    downscale, where the GSP gets 0x01001000); the presents looked right because the in-between
    image is shown on its own. After the fix: the crash run, 940 replays (930 refused on the
    title's lock wait, svc 0x22 at 0x12A150, 20 in-betweens after Start), no unknown address in
    the core log (crash-fix-1); the same with `interp leakcheck`, 940 checked, 0 leaking
    (crash-fix-leak.txt). In-betweens with the new flags, camera turning: OoT3D Kokiri Forest
    (scratch/runs/gx-kf-on `ladder-on-vs-off-14-19.png`, on above: every present changes, off
    0, 7272, 0, 7039) and MM3D South Clock Town (gx-sct-on `on-vs-off-14-19.png`: every present
    changes, off 0, 12248, 0, 12426), colours as the real frames'. The t = 1 probe and the leak
    check compare requests as read, so neither could see a wrong translation.
