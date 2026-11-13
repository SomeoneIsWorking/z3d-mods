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
  Azahar config and data, copied before any change. `env-assets/ref/`: the player's own
  oot3d-decomp and mm3d-decomp checkouts, read as references.

## Commands
- `uv run --frozen z3dmods rominfo <rom.3ds>`: title, remaster version and `code.bin` hash.
- `uv run --frozen z3dmods install oot3d mm3d`: install the sets into the player's Azahar.
- `flock <heavy-run-lock> uv run --frozen z3dmods run <game> <name> <steps...>`: the player's
  Azahar headless in a Gamescope headless session on the player's data; keys go in as XTest events
  on the player's keyboard bindings, frames and RAM come over Azahar's RPC server, and the player's
  Azahar config is restored afterwards. Steps: `wait:<s>`, `shot:<name>`, `<button>`,
  `hold:<button>,<s>`, `peek:<addr>,<size>`. `--settings '{"Layout":{"layout_option":1}}'` sets
  global settings for that run only. Output: `scratch/runs/<name>/`.
- `... z3dmods run <game> <name> --harness <steps...>`: the same steps on the libretro harness
  (frame-exact, savestates, watchpoints) on a copy of the player's data; see
  [docs/harness.md](docs/harness.md) for the core pin and build. Harness build:
  `cmake -S app -B build/app -G Ninja -DCMAKE_CXX_COMPILER=clang++ && ninja -C build/app`.

The OoT3D save in slot 1 is the player's: load it, never save over it. Check
`sdmc/.../title/00040000/00033500/data/00000001/` against the backup after a run.
