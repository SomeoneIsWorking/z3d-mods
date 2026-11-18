# A game set up in the picker can abort in the core's movie player

Seen 2026-11-18, `z3dmods inputtest setup-archive`: OoT3D set up from the picker boots, then the
app dies (SIGILL) within a second of "frame interpolation on". `z3d_core.log` ends in
`core/movie.cpp:operator():683: Assertion Failed!`, from `Movie::Handle` under
`HID::Module::UpdatePadCallback`: the movie is in `PlayMode::Playing` with no recorded input.
The rerun passed.

Cause: `Core::Movie::play_mode` (and `init_time`) in the core fork's `src/core/movie.h` have no
initialiser. Upstream's `System` is a static, so they start zeroed; the app makes each `System`
on the heap (`Emulator`, the picker's fresh one for a game set up just now), where they hold
whatever the allocation held. Fix in the fork: `PlayMode play_mode = PlayMode::None;` and
`u64 init_time = 0;`, then the core pin moved.
