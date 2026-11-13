# z3d app

User: "I had azahar libretro harness for testing", "I can play on the libretro version as well",
"this needs to be an app using Azahar libretro underneath", "everything is supposed to be VK".

## What it owns
`app/` builds `z3d`, a libretro frontend for Azahar's core, Vulkan only. It has two modes:

- Windowed (`z3dmods play <game>`): an SDL3 window on the player's session, sound, keyboard,
  gamepads and the mouse as the touch screen.
- `--hidden` (the tools): the window is never mapped and nothing is presented; the game runs
  frame by frame from stdin lines, one reply line each (`ok [value]` or `err <reason>`):

| Command | Does |
|---|---|
| `run <frames>` | runs frames with the held pad |
| `input <hex>` / `stick <x> <y>` | held joypad bits (`RETRO_DEVICE_ID_JOYPAD`), left stick |
| `r <va> <size>` / `w <va> <hex>` | guest memory of the live process: code, `.data`, `.bss`, heap |
| `savestate <file>` / `loadstate <file>` | the core's own states |
| `shot <file>` | the last presented frame as PNG |
| `watch <va> <size>` / `hits` / `unwatch` | guest CPU writes to a range, `address:size:value:pc:lr` |

The Azahar core maps face buttons by position: libretro A (east) is the 3DS A.

Windowed input: the keyboard uses the player's Azahar keys (a A, s B, z X, x Y, m Start, n Select,
q L, w R, 1 ZL, 2 ZR, t/g/f/h D-pad, arrows circle pad); gamepads by position (east A, south B);
the mouse on the bottom screen is the stylus.

## Core
`SomeoneIsWorking/azahar`, branch `z3d-harness`, revision `1b86acc48643b09ee02d02cf45fd87296dc1deec`:
upstream tag 2126.2 (the player's standalone version) plus two commits.

1. `core`: a write observer on `MemorySystem`, called for writes that land on watchpoint pages (the
   pages the GDB stub already uses, which have no host pointer, so JIT writes reach the slow path).
2. `libretro`: `src/citra_libretro/azahar_debug.h`, a frontend-private interface offered with
   `RETRO_ENVIRONMENT_PRIVATE | 1` (RetroArch and other frontends return false and see no change):
   guest reads and writes through the core's memory system, and write watches.

The stock memory map is not enough: it is taken at load, before the game allocates its heap, and
leaves out `.data` and `.bss`. With a stock core the harness still runs, on that map only.

Build (Clang, Ninja), then name the result in `env-assets/local.json` as `"libretro_core"`:

    cmake -S <azahar-checkout> -B <azahar-checkout>/build/libretro -G Ninja -DCMAKE_C_COMPILER=clang \
      -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release -DENABLE_LIBRETRO=ON -DENABLE_TESTS=OFF
    ninja -C <azahar-checkout>/build/libretro citra_libretro   # bin/Release/azahar_libretro.so

## Data flow
- Rendering: the core's Vulkan renderer (`RETRO_HW_CONTEXT_VULKAN`). The app makes the instance
  from the core's application info and lets the core create the device (negotiation interface v1)
  with the swapchain extension; the core hands over an image in `SHADER_READ_ONLY_OPTIMAL` on the
  same queue, which the presenter blits, letterboxed, into a FIFO swapchain, or reads back for `shot`.
- Sound: the core's batches go to an SDL audio stream; the windowed loop runs a frame whenever less
  than 50 ms is queued, so the game is paced by the sound card (by the clock without a device).
- User data: the core keeps it in `<user>/Azahar/`; `tools/z3dmods/app.py` seeds it with copies of
  the player's `nand`, `sdmc` and `sysdata` and a link to the installed `load/` (mods, textures).
  The harness makes a fresh one per run under `scratch/harness/<name>/`, so the player's data is
  only read and a file made in one run is gone in the next; states live in
  `scratch/harness/states/<name>/`. `z3dmods play` seeds `~/.local/share/z3d-mods/` once and keeps
  it: the app's saves are its own, apart from standalone Azahar's.
- Core options (`tools/z3dmods/core_options.py`): Vulkan, the set's screen layout, custom textures
  when the set has a pack, render scale 1 for the tools and 3 for play; every other option takes
  the core's own default.
- The core has no LLE-applets option (its setting stays at the default, on); the software keyboard
  for naming a file works there.

## Invariants
- One core per process (libretro callbacks carry no context).
- A state holds code memory: after `z3dmods install` rebuilds a patch, states made before it run
  the old patch. Make states in the same run that tests the patch.
- OoT3D: Start on a file created in the same session is ignored
  (docs/issues/harness-new-file-start.md).
- `shot` right after `loadstate` fails until a frame has run.
- Watch PC and LR are the running core's at the start of the translated block under the JIT; LR is
  stale there. Set `citra_use_cpu_jit=disabled` for exact values.

## Checking it
`uv run --frozen z3dmods play mm3d` opens the window; `z3dmods shortcut` adds a desktop shortcut per
game (this machine only), which builds `build/app` only when no build exists. The window's app id
(`--app-id z3d-<game>`) is the shortcut's name, which is how the desktop finds its icon.

- Verified 2026-11-13 on Vulkan: hidden, the MM3D title and "PRESS START" read back with the right
  colours; windowed from the MM3D shortcut on the KWin Wayland session, the title screen at 3x in a
  1530x1835 swapchain with the z3d icon, an uncorked PipeWire stream named `z3d`, emulated time
  keeping wall time, and a clean exit on SIGTERM.
- Not yet checked windowed: keyboard, gamepad and mouse input (no host events were injected into
  the player's desktop).

`uv run --frozen z3dmods run oot3d <name> --harness <steps...>` with the same steps as the standalone
runs, plus `savestate:<name>`, `loadstate:<name>`, `watch:<addr>,<size>`, `hits` and
`keydown:<button>`/`keyup:<button>` (held across steps) and `waitscript:<addr>[,<button>]`
(waits for that cutscene script, pressing the button meanwhile to advance text).
Standalone runs (no `--harness`) also take `type:<text>`, which types into Azahar's software
keyboard dialog and presses Return.

- Verified 2026-11-12: boot, title, file select and the player's save in Link's house with The One
  Screen HUD; from one state, the Kakariko skip and the watched run land where they do on standalone
  Azahar, and the save contexts differ only in `naviTimer` and the counter at +0x15B4; a watch on the
  day time (SaveContext+0x0C) reports one 2-byte write per game frame from 0x45DEA0.
- Not yet checked: the same state and inputs giving identical RAM twice; a watch on the cutscene
  frame counter (play+0x22B8) reported 4-byte float writes from 0x479084, not the counter's own
  increment, so heap watches need an interpreter run before they are trusted.
