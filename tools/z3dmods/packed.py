"""The packaged app's commands (package.py): `play`, which opens the title picker over the
player's dumps, and `setup`, the installer the picker runs. `python -m z3dmods.packed <command>`.
"""

from __future__ import annotations

import argparse
import json
from dataclasses import replace
from pathlib import Path

from z3dmods import config, play, setup


def _play(args: argparse.Namespace) -> None:
    cfg = config.load()
    cfg.app_data.mkdir(parents=True, exist_ok=True)
    folders = cfg.app_data / config.FOLDERS
    kept = json.loads(folders.read_text()) if folders.exists() else {}
    given = {name: str(folder.resolve()) for name, folder in (("roms", args.roms), ("mods", args.mods)) if folder}
    if given:
        kept |= given
        folders.write_text(json.dumps(kept, indent=2) + "\n")
        cfg = config.load()
    if "roms" not in kept:
        raise SystemExit("z3d does not know where your games are: start it once with --roms <folder>")
    roms = play.dumps(cfg, Path(kept["roms"]))
    if not roms:
        raise SystemExit(f"no dump in {kept['roms']} is of a game z3d has mods for")
    raise SystemExit(play.play(replace(cfg, roms=roms), None))


def _setup(args: argparse.Namespace) -> None:
    setup.setup(config.load(), args.rom, args.user, args.mods)


def main() -> None:
    parser = argparse.ArgumentParser(prog="z3d")
    commands = parser.add_subparsers(required=True)
    app = commands.add_parser("play", help="open the title picker")
    app.add_argument("--roms", type=Path, help="the folder of your 3DS dumps; kept for next time")
    app.add_argument("--mods", type=Path, help="the folder of your mod downloads; kept for next time")
    app.set_defaults(run=_play)
    made = commands.add_parser("setup", help="what the picker runs to set a game up")
    made.add_argument("--user", type=Path, required=True)
    made.add_argument("--mods", type=Path)
    made.add_argument("rom", type=Path)
    made.set_defaults(run=_setup)
    args = parser.parse_args()
    args.run(args)


if __name__ == "__main__":
    main()
