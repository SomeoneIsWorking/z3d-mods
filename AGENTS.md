# z3d-mods

Ocarina of Time 3D (USA, 0004000000033500) and Majora's Mask 3D (USA, 0004000000125500) on Azahar
2126.2, as openSU launches it. Azahar data: `~/.local/share/azahar-emu` (mods under
`load/mods/<titleID>/`). ROMs: the player's own dumps, named in `env-assets/local.json`. Third-party mod downloads live in gitignored
`env-assets/downloads/`, never in git; the tools find each by the hash its set names.

- What is installed and why: [docs/mods.md](docs/mods.md). Module map: [docs/codemap.md](docs/codemap.md).
- Plans: [docs/plans/](docs/plans/).

## Layout
- `sets/<game>.json`: a game's mod set (base mod, textures, global Azahar settings): each third-party
  source by name, where to get it and its SHA-256, never a path.
- `tools/z3dmods/`: the maintainer CLI (`uv run --frozen z3dmods ...`).
- `env-assets/local.json` (gitignored): ROM paths per title ID. `env-assets/downloads/<mod>/`: each
  third-party download in its own folder; a setup unpacks a .zip or .tar beside it, and a .7z or .rar is unpacked by hand (any layout works: sources are found by hash). `env-assets/backup/`: the player's
  Azahar config and data, copied before any change. `decomp/<game>/`: each game's
  decomp as a submodule (`git submodule update --init decomp/oot3d decomp/mm3d`, not recursive):
  guest addresses and RE findings live there, and a finding a change needs is committed to the
  decomp first.

## Commands
- `uv run --frozen z3dmods rominfo <rom.3ds>`: title, remaster version and `code.bin` hash.
- `uv run --frozen z3dmods install oot3d mm3d`: install the sets into the player's Azahar.
- `uv run --frozen z3dmods run <game> <name> <steps...>`: the player's
  Azahar headless in a Gamescope headless session on the player's data; keys go in as XTest events
  on the player's keyboard bindings, frames and RAM come over Azahar's RPC server, and the player's
  Azahar config is restored afterwards. Steps: `wait:<s>`, `shot:<name>`, `<button>`,
  `hold:<button>,<s>`, `peek:<addr>,<size>`. `--settings '{"Layout":{"layout_option":1}}'` sets
  global settings for that run only. Output: `scratch/runs/<name>/`.
- `... z3dmods run <game> <name> --harness <steps...>`: the same steps on the harness (the z3d
  app hidden: frame-exact, savestates, watchpoints) on a copy of the player's data;
  `--no-textures` turns the set's textures off. The app builds against the pinned Azahar fork's
  core (`"azahar_source"` in `env-assets/local.json`), installed once per revision under
  `azahar-core/` in the shared dependency prefix (`Config.dependencies`); see [docs/app.md](docs/app.md). Rebuild the app: `ninja -C build/app`.
- `... z3dmods play [game]`: the z3d app in a window with the installed sets: its title picker, or
  one game directly; its saves live in `~/.local/share/z3d-mods/`, apart from standalone Azahar's.
  `... z3dmods shortcut` adds the per-machine desktop shortcut (the picker); `--host ally` builds the
  package and installs it in `~/Games/z3d` on the Ally with its own shortcuts; the player's data there stays.
- `uv run --frozen z3dmods package`: the Linux package, `build/package/z3d` (a folder) and
  `build/package/z3d-x86_64.AppImage` ([docs/plans/packed-app.md](docs/plans/packed-app.md); "The release" there is how one is published).
- `uv run --frozen z3dmods sources`: on a machine without them, clones the checkouts a package is
  built from (the core, Lucent, setup-ui, RmlUi, port-assets) at their pinned revisions and
  downloads the AppImage runtime.
- `uv run --frozen z3dmods inputtest <oot3d|mm3d|picker|options|options-oot3d|slots|slots-small|slots-mm3d|slots-mm3d-small|verbs-mm3d|masks-mm3d|aim|aim-mm3d|setup|setup-wrong-dump|setup-missing-mod|setup-different-mod|setup-archive|menu-pad|first-run|first-run-appimage>`: the
  windowed app's input path with real key, mouse and pad events, off the player's desktop; `picker`
  runs the installed shortcut's Exec line, `first-run` the package `z3dmods package` built. Shots in `scratch/input-test/<scenario>/`.

The OoT3D save in slot 1 is the player's: load it, never save over it. Check
`sdmc/.../title/00040000/00033500/data/00000001/` against the backup after a run.
