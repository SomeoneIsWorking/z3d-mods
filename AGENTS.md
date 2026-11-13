# z3d-mods

Ocarina of Time 3D (USA, 0004000000033500) and Majora's Mask 3D (USA, 0004000000125500) on Azahar
2126.2, as openSU launches it. Azahar data: `~/.local/share/azahar-emu` (mods under
`load/mods/<titleID>/`). ROMs: `<roms>/`. Third-party mod downloads live in gitignored
`env-assets/`, never in git.

- What is installed and why: [docs/mods.md](docs/mods.md). Module map: [docs/codemap.md](docs/codemap.md).
- Plans: [docs/plans/](docs/plans/).

## Layout
- `sets/<game>.json`: a game's mod set (base mod, textures, global Azahar settings), paths relative
  to `env-assets/`.
- `tools/z3dmods/`: the maintainer CLI (`uv run --frozen z3dmods ...`).
- `env-assets/local.json` (gitignored): ROM paths per title ID. `env-assets/downloads/<mod>/`: each
  third-party download in its own folder, extracted into `x/`. `env-assets/backup/`: the player's
  Azahar config and data, copied before any change. `decomp/<game>/`: each game's
  decomp as a submodule (`git submodule update --init decomp/oot3d decomp/mm3d`, not recursive):
  guest addresses and RE findings live there, and a finding a change needs is committed to the
  decomp first.

## Commands
- `uv run --frozen z3dmods rominfo <rom.3ds>`: title, remaster version and `code.bin` hash.
- `uv run --frozen z3dmods install oot3d mm3d`: install the sets into the player's Azahar.
- `flock <heavy-run-lock> uv run --frozen z3dmods run <game> <name> <steps...>`: the player's
  Azahar headless in a Gamescope headless session on the player's data; keys go in as XTest events
  on the player's keyboard bindings, frames and RAM come over Azahar's RPC server, and the player's
  Azahar config is restored afterwards. Steps: `wait:<s>`, `shot:<name>`, `<button>`,
  `hold:<button>,<s>`, `peek:<addr>,<size>`. `--settings '{"Layout":{"layout_option":1}}'` sets
  global settings for that run only. Output: `scratch/runs/<name>/`.
- `... z3dmods run <game> <name> --harness <steps...>`: the same steps on the harness (the z3d
  app hidden: frame-exact, savestates, watchpoints) on a copy of the player's data;
  `--no-textures` turns the set's textures off. The app builds against the pinned Azahar fork's
  core (`"azahar_source"` in `env-assets/local.json`), installed once per revision under
  `<dependencies>/azahar-core/`; see [docs/app.md](docs/app.md). Rebuild the app: `ninja -C build/app`.
- `... z3dmods play <game>`: the z3d app in a window with the installed set; its saves live in
  `~/.local/share/z3d-mods/`, apart from standalone Azahar's. `... z3dmods shortcut` adds the
  per-machine desktop shortcuts.
- `flock <heavy-run-lock> uv run --frozen z3dmods inputtest <game>`: the windowed app's
  input path with real key, mouse and pad events, off the player's desktop; shots in
  `scratch/input-test/<game>/`.

The OoT3D save in slot 1 is the player's: load it, never save over it. Check
`sdmc/.../title/00040000/00033500/data/00000001/` against the backup after a run.
