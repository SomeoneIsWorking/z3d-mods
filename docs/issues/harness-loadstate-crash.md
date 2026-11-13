# Harness: loadstate after a scene change crashes the app on the Ally

Observed 2026-11-13 on the Ally (Vulkan, hidden headless Gamescope): `savestate:house` in Link's
house, a warp to 0xEE and its cutscene played to scene 71, then `loadstate:house` ends the app with
`Unhandled SIGSEGV` in harness.log. The same savestate/loadstate pair worked here (stairs-tour,
loads after short warps).

Expected: the state loads and the run goes on.

Repro: `z3dmods run oot3d x --harness wait:20 start wait:3 a wait:2 a wait:8 savestate:house
poke:0x588EF8,f1ff warp:0xEE waitcs:300,a wait:6 loadstate:house wait:0.5`

Cause: not traced. Runs that need two states use two processes for now.
