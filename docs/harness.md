# Libretro Azahar harness

User: "I had azahar libretro harness for testing", "we can make a new one", "if you think we should
have it, I have tokens to spare", and "I can play on the libretro version as well".

## What it owns
`harness/` builds `z3d_harness`, a headless libretro frontend for Azahar's core. It runs a 3DS game
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
- Rendering: the core's OpenGL renderer into a framebuffer on a windowless EGL context
  (surfaceless Mesa, the real GPU). About 8x real time on the RX 6700 XT; the software renderer ran
  at 8 fps.
- User data: the core keeps it in `<save dir>/Azahar/`. `tools/z3dmods/harness.py` points the save
  dir at `scratch/harness/<game>/` and copies the player's `nand`, `sdmc` and `sysdata` there before each
  run and links the installed `load/` (mods, textures), so the player's data is only read and a
  file made in one run is gone in the next. States live in
  `scratch/harness/states/<game>/`.
- Core options: OpenGL, single-screen layout; every other option takes the core's own default from
  its option definitions.

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
`uv run --frozen z3dmods run oot3d <name> --harness <steps...>` with the same steps as the standalone
runs, plus `savestate:<name>`, `loadstate:<name>`, `watch:<addr>,<size>`, `hits` and
`keydown:<button>`/`keyup:<button>` (held across steps).

- Verified 2026-11-12: boot, title, file select and the player's save in Link's house with The One
  Screen HUD; from one state, the Kakariko skip and the watched run land where they do on standalone
  Azahar, and the save contexts differ only in `naviTimer` and the counter at +0x15B4; a watch on the
  day time (SaveContext+0x0C) reports one 2-byte write per game frame from 0x45DEA0.
- Not yet checked: the same state and inputs giving identical RAM twice; a watch on the cutscene
  frame counter (play+0x22B8) reported 4-byte float writes from 0x479084, not the counter's own
  increment, so heap watches need an interpreter run before they are trusted.
