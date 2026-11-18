# The title stage

The format is the Slant Stage (the `slant-stage` skill holds it); this plan holds what is
particular to this app and Azahar.

The title picker becomes both games running side by side (user, 2026-11-17): "Both games running
side by side with a slanted separator, selected game is wider, other game is gray and paused,
whenever you pick, slant animates into the side and the game you picked becomes full and the other
game is stopped", and "just run both cores simultaneously ... the loop only advances the selected
game, then after selecting, you can unload the other one".

## Behaviour

- At start both games that are set up are loaded, each its own emulated system with its own set,
  options and saves. Only the selected one is stepped; its sound plays. The other is not stepped:
  its last frame is shown grey.
- The window is split by a slanted edge; the selected game's side is the wider one. Left and right
  (pad, keys, mouse hover) move the selection; the edge slides to the other side, the newly
  selected game starts stepping from where it stopped and the other freezes and greys.
- A (Enter, a click on the selected side) picks: the edge slides out of the window, the picked
  game fills it and plays on from its frame without a reload; the other system is unloaded.
- X opens that game's options as today; a game not set up shows its icon and "Set up" on its side
  instead of a running game, and picking it runs the setup as today.
- The game's name and the hints are drawn on a band at the bottom, in the menus' outline font.

## Design

1. **Two systems in the core** (the Azahar fork): `Core::System`'s static instance stays the
   default behind a current-system pointer (`System::MakeCurrent`) that `GetInstance`,
   `Core::Global<>` and the remaining singleton calls read. Each `Emulator` owns its own `System`
   and makes it current before it loads, steps, saves or shuts it down.
2. **The window stays the canvas's**: `Settings::values.present_to_window` off keeps the core's
   Vulkan renderer from presenting and from making a swapchain on the window (made on the first
   present instead); frames still render, and the stepped game's top screen is read back each
   frame (`Emulator::Capture::Top`).
3. **The picker** (`title_picker`) draws the stage with the menu canvas: each side a slanted
   four-cornered shape with its game's frame covering it (the held one greyed once, when it
   stops), the edge's line, then a band with the titles and the hints. The games' input is muted
   (`PadMode::None`) and the picker pumps the events itself (`GameWindow::setMenuPumps`).
4. **The pick**: the edge eases out past the window's side while the picked game keeps running;
   the picker hands that `Emulator` to the play loop and destroys the other, and the core takes
   the window on its next frame. A game set up just now has no system yet and is loaded by the
   play loop as before.

## Steps

1. The fork: current-system pointer and `present_to_window`.
2. The app: `Emulator` owns its system; the picker's stage, its animation and the pick into play.
3. The input tests `picker` and `first-run` on the stage.

## Status

Built 2026-11-17. Seen (`z3dmods inputtest stage`, keys, frames in `scratch/input-test/stage/`):
OoT3D's title runs on the wide left side; Right slides the edge and MM3D runs while OoT3D holds
grey at its last frame; Left brings OoT3D back moving on from where it stopped; Right and A pick
MM3D, the edge slides out and MM3D plays on at its title screen, not booted again. `inputtest
picker` picks OoT3D into play and returns, and on 2026-11-18 the test pad's stick selects MM3D,
its A boots it and Guide returns.
