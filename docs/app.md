# z3d app

User: "I had azahar libretro harness for testing", "this needs to be an app using Azahar libretro
underneath", "everything is supposed to be VK", "you don't have to use the libretro core, it can be
Azahar's core without qt".

## What it owns
`app/` builds `z3d`, a frontend of Azahar's core linked in directly (no Qt, no libretro), Vulkan
only. It links the core's shared library, so it is GPL-2.0-or-later as linked. Two modes:

- Windowed (`z3dmods play [game]`): an SDL3 window on the player's session, sound, keyboard,
  gamepads and the mouse as the touch screen. Without a game it opens the title picker: each
  game's icon and title read from the player's own dump (its SMDH), chosen with left/right and A
  or Start on the pad or keyboard, or a click; X opens the selected game's options. Escape or the gamepad's guide button in a game shuts
  it down and returns to the picker; closing the window quits.
- `--hidden` (the tools): the window is never mapped, there is no sound and no pacing, and custom
  textures load before the frame that needs them (asynchronous loading falls behind an unpaced run); the game
  runs frame by frame from stdin lines, one reply line each (`ok [value]` or `err <reason>`):

| Command | Does |
|---|---|
| `run <frames>` | runs frames with the held pad and keeps the last one for `shot` |
| `input <hex>` / `stick <x> <y>` / `cstick <x> <y>` | held buttons (bits in `Settings::NativeButton` order), circle pad and C-stick (signed 16-bit, y down) |
| `device <pad\|keyboard>` | which device the held input stands for; the HUD's prompts follow it |
| `gyro <x> <y> <z>` | the 3DS turning from now on, degrees a second of the game's time on the core's axes (x left along the D-pad, y out of the touch screen, z up along the D-pad) |
| `mouse <dx> <dy>` | the mouse moved by that many counts (x right, y down) for the aim, as a window's mouse motion does; an error unless the look follows the gyroscope |
| `r <va> <size>` / `w <va> <hex>` | guest memory of the running process: code, `.data`, `.bss`, heap |
| `savestate <file>` / `loadstate <file>` | the core's own states |
| `shot <file>` | the last frame of the last `run` as PNG, in the set's layout at the render scale |
| `watch <va> <size>` / `hits` / `unwatch` | guest CPU writes to a range, `address:size:value:pc:lr` |
| `touch <x> <y>` / `touch off` | the stylus held at a pixel of the 320x240 touch screen, wherever the layout draws it, or lifted |
| `option <id> <value>` | sets a game option and writes the patch's block (Options below) |
| `patch on` / `patch off` / `textures on` / `textures off` | swaps the running game's mods (Hotswap below) |

Flags (`src/app_config.hpp`): `--user`, then either `--rom` with `--layout`
(`default|single_screen|large_screen|side_by_side|overlay`), `--textures` and `--options <json>`, or `--library <json>` and
`--font <ttf>` for the picker (never `--hidden`); `--title`, `--app-id`, `--resolution`, `--hidden`,
`--dump-textures` (the core writes every texture without a replacement as PNG under
`<user>/Azahar/dump/textures/<title id>/`; `z3dmods run ... --harness --dump-textures`).
`tools/z3dmods/app.py` derives a game's from its set: its `screen_layout`, textures when it has a
pack, render scale 1 for the tools and 3 for play. `play.py` writes the library,
`<user>/library.json`, an array of `{"rom", "layout", "textures", "options"}` per set, and takes the font from
fontconfig (`Noto Sans:bold`).

Setup: with `--installer <program> [<argument>]...` (the rest of the command line) the picker
sets up a game whose options manifest is missing from the user folder: `SetInstall`
(`src/set_install`) runs the installer with the dump's path added and the picker shows its last
printed line until it ends, then boots the game; when it fails the picker shows the installer's
refusal (its lines that start with "! "), shrunk until it fits, until A. `play.py` passes
`z3dmods setup --user <user> --mods <the player's mod downloads>` (plans/packed-app.md).

Windowed input: the keyboard uses the player's Azahar keys (a A, s B, z X, x Y, m Start, n Select,
q L, w R, e ZL, r ZR, t/g/f/h D-pad, arrows circle pad); gamepads by the names on an Xbox pad,
(A south, B east, X west, Y north, LT and RT are L and R, RB is ZR, the right stick's click ZL),
right stick the C-stick; the left mouse button on the bottom screen is the stylus. LB is the
chord: it and the Tab key hold pad bit 12 (the core's Debug button), which no 3DS button sets and
our patches read as "second item page"; while LB is held, A, B, X and Y press that page's four
item buttons and nothing else (`kChords` in `pad_input.cpp`, plans/pc-controls-hud.md). The keys
1 to 8 are the eight item slots: 1 to 4 press the chord layout's four item buttons, 5 to 8 press
them with bit 12 held (`kItemKeys`). `PadInput::lastDevice()` is the device a button or key was
last held on, the keyboard when both are; the HUD's prompts follow it.

Mouse aim (`mouse_aim`, plans/pc-controls-hud.md): while the game's view is first person and
follows the gyroscope (the first-person look, an aimed bow or Slingshot; both `PLAYER_LOOK_*`
flags of the decomp's `docs/motion.md`), `MouseAim` asks the window for the mouse
(`GameWindow::captureMouse`: SDL's relative mode, the pointer hidden and held, the stylus
lifted) and turns the 3DS by its motion, so a count turns the view 0.05 degrees
(`kDegreesPerCount`) on both axes; when the look ends, the game shuts down or the window is
hidden, the mouse is the stylus again. How it is exact:
- The core asks our motion device for every HID gyroscope sample
  (`Input::MotionDevice::TakeGyroscopeSample`, Core 7) and takes it as it is; `PadInput`'s device
  answers with `PadState::gyro` plus its `GyroSource`, which is the `MouseAim` of the running game.
- Once a frame `MouseAim` reads the game through the addresses the install writes into
  `z3d-options.json` as `"aim"` (`patch/<game>/aim.toml`, the decomp's names) and keeps the SDK
  reader integrating samples as they are: zero play, zero drift and the accelerometer's revise
  off, the drift radius negative (no averaging of like samples), the bias 0 and the calibration
  scale 1. The app writes these through `gMotionState.reader` rather than a patch: our device
  exists only in the app, so it holds with the patch out, after a `loadstate` and a hotswap,
  and standalone Azahar with a real gyroscope keeps the game's own filters.
- Mouse counts become degrees the 3DS still has to turn: view degrees divided by the game's own
  `yawScale` and `pitchScale` (2.1 and 1.46). Each HID sample turns it around one axis only, its
  x (pitch) or world up (yaw, from the pitch fed so far; read back from the reader's matrix
  whenever no sample has turned it for 32 samples), 6 degrees at most, in whole units of the
  gyroscope with the remainder carried: nothing is lost or doubled, and a mouse faster than 600
  degrees of the 3DS a second is finished over the next samples.
- At a limit of the look (the view is held while the 3DS turns on) motion further past it is
  dropped, and once the game has read every sample the 3DS comes back to the limit, so the way
  back starts at once.
- Not covered: Master Quest's mirrored yaw, a look that starts without the first-person draw,
  and mouse buttons (the item's own button fires).

Seen: on the harness (`mouse:<dx>,<dy>`), MM3D at South Clock Town's owl statue (`aim-mm5`) and
OoT3D inside the Deku Tree (`aim-oot3`), first person and with the bow or Slingshot aimed: 400
counts right turn `focus.rot` 19.995 and 19.990 degrees for 20, 800 left 39.974 to 39.985 for
40, 300 down 14.996 for 15, 600 either way while pitched 15 degrees up or down 29.965 to 29.998
for 30 with the pitch within 0.005, and a diagonal of 437 by 211 counts 21.846 and 10.547 for
21.85 and 10.55; `focus.rot` counts in steps of 0.0055 degrees and cuts. 2000 counts down stop
at the limit, 76.9 degrees, and 100 up after a pause come back 4.999. In a window with real
mouse motion (`z3dmods inputtest aim`, `aim-mm3d`): D-pad Left or ZL logs "mouse captured for
aim", the same four moves show the same views as the harness frames, and leaving first person
logs "mouse released after 1600 by 600 counts", the travel the test sent.

The picker and its menus do not read the 3DS pad: `PadInput::menu()` (`MenuPad`) is the host's
buttons by their names on an Xbox pad (A, B, X, Y, Start, the D-pad and the left stick) and the
keys that stand for them, taken before a game's chord layout or a set's X/Y trade, so a menu
button is the same whichever game was played last.

## Options
Each game has its own options, opened with X on its picker tile (`options_menu`): Up and Down pick
a row, A, Left or Right change it, B or X goes back. `GameOptions` owns them:

- The patch's switches come from the manifest the install writes beside the set,
  `load/mods/<title id>/z3d-options.json` (`tools/z3dmods/options.py`, from
  `patch/<game>/options.toml`): the guest address of the patch's options block and each switch's
  id, label and default, one byte each in order. `--options` and the library's `"options"` name it
  relative to the user folder.
- An `[[option]]` with `pad` instead of `field` is the app's: `cstick_invert_x` and
  `cstick_invert_y` turn the C-stick around on its way to the game (MM3D's camera stick, which the
  game reads itself).
- An `[[option]]` with `layer` names a folder of replacement game files beside the set's `romfs/`
  (`load/mods/<title id>/<layer>/`, written by the install): on, the app lists it in the core's
  `Settings::values.romfs_layers` and the core stacks it over `romfs/` when the game loads
  (OoT3D's stepped stairs). The manifest leaves the switch out when the install wrote no such
  folder. Standalone Azahar never loads a layer.
- The app's own rows follow: the screen layout, and the texture pack when the set has one.
- Choices are kept per game in `<user>/options/<rom name>.json`. The tools (`--hidden`) keep none
  and run every game on its defaults; a run changes one with the `option` command
  (`option:<id>,<value>` step), or before boot with `--option <id>=<value>`
  (`z3dmods run ... --harness --option stairs=0`), which is the only way for one that takes
  effect at boot (a layer, the layout, the textures).
- At boot the app applies layout and textures through `applyGameSettings` and writes the block once
  the game is loaded; `loadstate` writes it again, since a state carries the block of the run that
  made it.
- The menu lives on the picker because the core's renderer owns the window while a game runs: a
  change takes effect the next time the game boots.

Adding a switch: an `[[option]]` in `patch/<game>/options.toml`, then read `options().<field>` from
the generated `options.hpp` in the feature; the menu, the manifest and the stored file follow.

## Hotswap
`ModSwap` changes a running game's mods without a reboot, between two frames:

- The patch: `patch on` writes the set's `code.ips` as it is on disk now, so a patch rebuilt by
  `z3dmods install` replaces the running one; `patch off` puts the game's own code back. It
  builds the code image the way the core's loader does (the loader's unpatched code, the set's
  `code.bin` included, padded to the process image, then the core's own IPS reader), writes only
  the bytes either patch changes below the patch's own pages, writes those pages whole (they
  start at the options block, and their `.bss` starts over), and clears the CPU's translated
  code. The options are applied again afterwards.
- The textures: `textures on|off` sets the core's custom-texture switch; its cache is dropped and
  refilled from the next frame. The core lists a pack's files once, so a pack changed on disk
  still needs a reboot.
- In the window: F5 reads the patch again, F6 takes it out or puts it back, F7 turns the textures
  off or on. Run steps: `patch:<on|off>`, `textures:<on|off>`.
- A patch that keeps state in the game's own data is swapped as it stands: take the patch out
  with the chord button released, or the second item page stays on the buttons.
- Not swapped: the base mod (`code.bin`, `exheader.bin`), `romfs/` and its layers, which the game
  reads as it loads.

Seen (`mm-swap1`, South Clock Town's owl statue, a bow on X): with the patch in, the chord button
empties X; `patch off` and 0x317000 reads the game's own instruction and the chord does nothing;
`patch on` and the hook is back, the chord empties X again and the statue still saves.
`textures off` draws the 3DS's A on the action button, `textures on` the green A. In the window
(`z3dmods inputtest hotswap`, real F7, F6 and F5 keys): the log names each swap ("patch out: 4112
bytes written", "patch in") and the Lost Woods cutscene plays on through them.

## Overlay layout
`overlay` is the app's own single screen for a game that keeps its HUD and menus on the touch
screen (MM3D's set uses it; `screen_overlay`). The window has the top screen's shape. A layout
places the top screen and pieces of the touch screen in it: a piece is a rectangle of the touch
screen's 320x240 pixels drawn at a rectangle of the top screen's 400x240, with rounded corners and
an opacity. The game's table, `patch/<game>/overlay.toml`, holds the layouts and names one for each
of the game's touch screens (and one for while its HUD visibility hides that screen); MM3D's:

- `play` (the main touch screen): the top screen alone, under our own HUD (`hud = true`; "HUD"
  below). No part of the touch screen is drawn in play;
- `menu` (Items, Masks, Gear): the whole touch screen, large, under the menu's title bar;
- `top_only` (the game blanks the touch screen, or its HUD visibility hides the main one: an
  area's title card, a cutscene): the top screen alone;
- `ocarina`: the whole touch screen as a small inset, since the notes play on the top screen;
- `split`, the app's own (`splitLayout`): the two screens side by side, for the map, the
  notebook and anything outside play (boot, title, file select).

The install writes the table into `z3d-options.json` as `"screens"`, with the address of the
game's pointer to the touch screen that is up, the table of play's screens and the HUD visibility
word (from the decomp, `tools/z3dmods/games.py`). `ScreenOverlay` reads the game after every frame
and hands the layout to `GameWindow`, which writes the core's custom layout and
`custom_bottom_pieces` (`placeOverlay`). The mouse goes back through the same pieces
(`overlayTouch`): a click on a piece touches the touch-screen pixel drawn there, and a click
between pieces touches nothing. A tool screenshot of the side-by-side layout is cut to the
screens' extent (800x384 at scale 2).

Seen (`mm-ov3`, scale 3): the Items and Masks menus, and Woodfall Temple's title card with the
top screen alone.

## HUD
`hud` is the play HUD's one owner, for every game (plans/pc-controls-hud.md). It knows element
kinds, not games: `hearts` (a row of hearts filled by a health value), `meter` (a bar filled by a
value), `counter` (an icon and a number) and `button` (a round or square glass frame, the item's
icon, and the pad's glyph). A game is `patch/<game>/hud.toml`: each element's kind, its rectangle
on the top screen's 400x240 and the guest memory it reads, written as a sum of the decomp's names
(`at = ["gCommonData", "CommonData.save", ...]`); the install resolves the sums and writes them
into `z3d-options.json` as `"hud"`.

The art is port-assets' (`--assets <port-assets copy>`: `sets/hud`, `sets/gamepad-xbox360`, the
key typeface): `hud_atlas` rasterizes each SVG at the size it is drawn at in the window, once per
window size, into one RGBA image, and `Hud` gives the core that image and a list of quads every
frame (`Settings::values.overlay_image`, `overlay_quads`). It reads the game after every frame
through `Emulator::read`, and draws only while the overlay's layout is play's (`hud = true` in
`overlay.toml`), or, for a game without an overlay table, while every condition of `hud.toml`'s
`show` holds. A value can sit behind a guest pointer (`through`), and an element is drawn only
while its `when` conditions hold. While the chord is held (`PadInput::chord()`), a button shows the face button
that presses it; with the keyboard last used it shows a key cap instead (1 to 4, or 5 to 8 on
the second page). An item's icon is `<mods>/z3d-hud-icons/<item id>.png`, cut at install from the game's icon sheet
(the pack's copy when it has one) by the game's own table of a frame per item (`hud_icons.py`,
`[icons]` in `hud.toml`). Where the game has no such table an item's frame is its id (OoT3D). A button also draws its
item's count, dims a disabled item and writes the word of the game's action number under it
(`label`, `labels`); a counter can read one entry of an array (`index`); a meter can double its
length.

Seen (MM3D, `mm-hud2` at scale 3, `mm-hud3` at scale 5): three hearts and 0 rupees on a fresh
file; `healthCapacity` 0xA0, `health` 0x68 and `rupees` 123 written, and the next frame has ten
hearts with six and a half red and the counter at 123; the chord held puts A and B on the I and
II buttons; `device:keyboard` turns the glyphs into caps 1 to 4, and 5 to 8 with the chord. The
same crops at both sizes have clean edges.
`mm-hud4`: the bow, a bomb, the powder keg and the fire arrow written onto X, Y, I and II draw
their icons (the pack's) with the counts from `SaveData.ammo`, the half-full magic meter shows
once `magicAcquired` is set, and after Y pulls a bomb that goes off in Link's hands the hearts
read two and a half.
OoT3D (`mm-oothud3`, scale 3): the same values written give ten hearts with six and a half red,
the half meter and 123 rupees; a bomb, the Slingshot, nuts and sticks written onto the buttons
draw their icons with 9, 17, 7 and 5; "Check" sits under A beside the bed; in the Forest Temple
the key counter reads the 2 written to its dungeon; the chord puts A and B on I and II; the
pause screen hides the HUD. The One Screen's HUD is not drawn in any of them.

## Core
`SomeoneIsWorking/azahar`, branch `z3d-harness`, pinned in `tools/z3dmods/app.py`
(`AZAHAR_REVISION`): upstream tag 2126.2 (the player's standalone version) plus our commits.

1. `core`: a write observer on `MemorySystem`, called for writes that land on watchpoint pages (the
   pages the GDB stub already uses, which have no host pointer, so JIT writes reach the slow path).
2. `cmake`: `AZAHAR_CORE_PACKAGE` (`src/core_package/`), which installs the core as one shared
   library, `libazahar_core.so` (the four core libraries and their static externals, built
   position-independent), its headers and those its public headers include, and a CMake package
   with the core's compile definitions: `find_package(AzaharCore CONFIG)`.
3. `rasterizer_cache`: `ValidateSurface` looks its surface up again after a custom texture upload.
   With `async_custom_loading` off (the tools) the upload runs inside it and replaces the surface,
   which can move the surface storage; the held reference then crashed the app (SIGSEGV in
   `boost::icl` under `ValidateSurface`), at boot or after a `loadstate`, on a set with a texture
   pack.
4. `renderer_vulkan`: a screenshot whose buffer the driver will not import as host memory is
   taken through the staging copy. `TryRenderScreenshotWithHostMemory` let that refusal throw
   (`vk::Device::allocateMemoryUnique: ErrorInvalidExternalHandle`), which ended a tool run now
   and then; radv refuses pages the kernel cannot pin, file-backed ones every time.
5. `renderer_vulkan`: `Settings::values.custom_bottom_pieces` (`Settings::ScreenPiece`): with the
   custom layout, the bottom screen is drawn as these pieces (a source rectangle of the screen at
   a destination rectangle of the window, corner radius, opacity) instead of whole. The present
   shaders compute each rectangle's coverage, which also carries the second layer's opacity.
6. `renderer_vulkan`: `Settings::values.overlay_image` (RGBA, uploaded when its generation
   changes) and `overlay_quads` (a rectangle of the image at a rectangle of the window), drawn
   after the screens as the present shaders' fourth texture. Tool screenshots include them.

7. `hid`: `Input::MotionDevice::TakeGyroscopeSample`. A motion device that overrides it is
   asked once per HID gyroscope sample and its rate is taken as it is, rounded to the HID's unit;
   every other device is read as before, its rate stretched by the host's last frame time
   (which made a tool's unpaced run feed a tenth of the rate, unevenly).

The checkout is named in `env-assets/local.json` as `"azahar_source"`; `app.py` refuses one that is
not at the pinned revision. `app.core` builds and installs the core once per revision and
toolchain into `<dependencies>/azahar-core/<revision>-clang/prefix` (`Config.dependencies`, build tree beside it), with
`AZAHAR_OPTIONS` (no Qt, SDL2, libretro, OpenGL, software renderer, OpenAL, tests or network
services). `app.binary` then configures `app/` on its own into `build/app` with
`-DCMAKE_PREFIX_PATH=<prefix>` (Clang, Ninja); `build/app` holds only the app's and Lucent's
objects. Rebuild the app: `ninja -C build/app`.

## Data flow
- `main` composes: `applyCoreSettings` (settings and user path, before anything reads them),
  `CoreLog`, `PadInput` (the core's `z3d` input engine), `GameWindow` (the core's
  `EmuWindow`), `Emulator` (load, frames, memory, states, watches), `ModSwap`, then `Player` or
  `Repl`. With the picker: `GameLibrary` and a `GameOptions` per game, then per game `TitlePicker`
  (and its `OptionsMenu`), `applyGameSettings`, `GameWindow::boot` (title and the layout's window
  shape), a new `Emulator`, `GameOptions::apply` and `Player`.
- The picker draws with an SDL renderer (Vulkan driver) and SDL3_ttf text on the game window, and
  destroys it before the core's renderer takes the window; the core shuts down
  (`System::Shutdown`) before the picker opens again.
- Rendering: the core's own Vulkan renderer presents to the window's native surface (Wayland or
  X11, from SDL3's window properties). A frame ends when the renderer calls
  `GameWindow::PollEvents`, which counts it, pumps SDL events and reads the host pad. `shot` frames
  come from the renderer's screenshot path (XRGB rows), requested before the last frame of a `run`.
- Sound: the core's own sink (cubeb) when windowed, the null sink when hidden. The windowed loop is
  paced by the core's frame limiter at 100%.
- Input: every 3DS button, both sticks and the motion device are `engine:z3d` devices on one
  shared pad state; touch is the window's `engine:emu_window`.
- User data: the core keeps it in `<user>/Azahar/` (its user path); `app.py` seeds it with copies
  of the player's `nand`, `sdmc` and `sysdata` and a link to the installed `load/` (mods, textures).
  The harness makes a fresh one per run under `scratch/harness/<name>/`, so the player's data is
  only read and a file made in one run is gone in the next; states live in
  `scratch/harness/states/<name>/`. `z3dmods play` seeds `~/.local/share/z3d-mods/` once and keeps
  it: the app's saves are its own, apart from standalone Azahar's. The core's log is
  `<user>/Azahar/log/z3d_core.log`.
- Applets: the core's default ones (the software keyboard enters the console's user name).

## Invariants
- One core per process (`Core::System` is a singleton).
- A state holds code memory: after `z3dmods install` rebuilds a patch, states made before it run
  the old patch. Make states in the same run that tests the patch.
- `loadstate` clears watches; set them again after it.
- Watch PC and LR are the running core's at the start of the translated block under the JIT; LR is
  stale there.

## Checking it
`uv run --frozen z3dmods play` opens the picker (`play mm3d` one game); `z3dmods shortcut` adds the
`z3d` desktop shortcut (this machine only: `~/.local/share/applications` and `~/Desktop`), which
runs `z3dmods play` and so builds `build/app` only when no build exists. The window's app id
(`--app-id z3d`) is the shortcut's name, which is how the desktop finds its icon.

`z3dmods shortcut --host ally` builds the package (plans/packed-app.md, "The package") and installs
it on that machine instead (`tools/z3dmods/host_install.py`): `~/Games/z3d/` is the package, and
`user/` in it is the player's data, which stays: `folders.json` (the dumps in the host's
`~/ROM/3DS/`), the mod downloads in `user/mods/`, the sets its picker makes from them and the
app's saves (copied from here only when the host has none). `user/` without the sets and the
downloads is copied to `~/Games/z3d-backups/user-<time>.tar` first. Its menu and Desktop entries
run the package's launcher. Re-run it after a change.

- Picker, 2026-11-13, `z3dmods inputtest picker` (the installed shortcut's Exec line in a headless
  Gamescope, `Z3D_APP_DATA` on a fresh copy of the player's data): both icons and titles from the
  dumps; the A key boots OoT3D to its title, Escape returns; the pad's stick selects MM3D and its A
  boots it, the guide button returns; a click on the OoT3D tile boots it again.
- Options, 2026-11-14, `z3dmods inputtest options-oot3d` (the shortcut's picker, real keys): X on
  the OoT3D tile opens its options with every row of the manifest; Down and A set Quick Text to
  Off, and File 1 boots. The test waits on the menus' own log lines (`[menu] picker: on <title>`,
  `options: row <id>`, `options: <id> = <choice>`), not on fixed times: five runs in a row took
  the same path. What the option does in game is shown on the harness (docs/plans/oot3d-qol.md).
- Ally install, 2026-11-13: the installed entry's Exec line in a hidden headless Gamescope there
  shows the picker with both games; the A key boots OoT3D to its 4K title screen.

- Verified 2026-11-13 on the core linked directly (Vulkan, RADV): hidden, the MM3D intro and its
  bottom screen read back at 400x480, a 20 MB savestate saved and loaded, 1260 frames for 21 s of
  steps; OoT3D single-screen at 400x240, Start reaching file select with the player's "Link" save,
  guest code and `gGameInfo` read. The unmapped Wayland window presents without stalling.
  Windowed from `z3dmods play mm3d` on the KWin Wayland session: the intro at 3x in a window titled
  "Majora's Mask 3D" with the z3d icon, custom textures loading, a running PipeWire stream named
  `z3d`, and a clean exit on SIGTERM.
- Input, 2026-11-13, `z3dmods inputtest <game>` (the windowed app as `z3dmods play` runs it, in a
  headless Gamescope on a fresh copy of the player's data; XTest keys and mouse on its X server, a
  uinput gamepad): OoT3D, keys reach file select and File 1, the arrow keys and then the pad's left
  stick each move Link on the single screen; MM3D, keys reach file select, a click on slot 2 of the
  bottom screen makes a file there, and Start on the opening narration lands in the Lost Woods.
  Shots in `scratch/input-test/<game>/`. The steps are timed in real time, so a slow boot can
  leave a step on the wrong screen; read the shots.

`uv run --frozen z3dmods run oot3d <name> --harness <steps...>` with the same steps as the standalone
runs, plus `savestate:<name>`, `loadstate:<name>`, `watch:<addr>,<size>`, `hits` and
`keydown:<button>`/`keyup:<button>` (held across steps) and `waitscript:<addr>[,<button>]`
(waits for that cutscene script, pressing the button meanwhile to advance text).
Standalone runs (no `--harness`) also take `type:<text>`, which types into Azahar's software
keyboard dialog and presses Return.

- Verified 2026-11-12 (on the earlier libretro build): boot, title, file select and the player's save in Link's house with The One
  Screen HUD; from one state, the Kakariko skip and the watched run land where they do on standalone
  Azahar, and the save contexts differ only in `naviTimer` and the counter at +0x15B4; a watch on the
  day time (SaveContext+0x0C) reports one 2-byte write per game frame from 0x45DEA0.
- Not yet checked: the same state and inputs giving identical RAM twice; a watch on the cutscene
  frame counter (play+0x22B8) reported 4-byte float writes from 0x479084, not the counter's own
  increment, so heap watches need an interpreter run before they are trusted.
