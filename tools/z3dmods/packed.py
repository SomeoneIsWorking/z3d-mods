"""The packaged app's commands (package.py), `python -m z3dmods.packed <command>`: `play`, which
opens the title picker over the player's dumps, and the two the picker runs: `folders`, which
takes a folder the player chose (or, with `--kept`, prints the one set now), and `setup`, which
makes a game's set.
"""

from __future__ import annotations

import argparse
import json
from dataclasses import replace
from pathlib import Path

from z3dmods import config, play, setup, source_check
from z3dmods.config import Config


def _kept(user: Path) -> dict[str, str]:
    """The player's folders (config.FOLDERS) in the user folder `user`."""
    path = user / config.FOLDERS
    return json.loads(path.read_text()) if path.exists() else {}


def _keep(user: Path, folders: dict[str, str]) -> None:
    user.mkdir(parents=True, exist_ok=True)
    (user / config.FOLDERS).write_text(json.dumps(folders, indent=2) + "\n")


def _with_dumps(cfg: Config, folders: dict[str, str]) -> Config:
    """`cfg` with the dumps in the player's dump folder that a set is made for."""
    roms = Path(folders["roms"]) if "roms" in folders else None
    return replace(cfg, roms=play.dumps(cfg, roms) if roms and roms.is_dir() else {})


def _play(args: argparse.Namespace) -> None:
    cfg = config.load()
    kept = _kept(cfg.app_data)
    given = {name: str(folder.resolve()) for name, folder in (("roms", args.roms), ("mods", args.mods)) if folder}
    if given:
        kept |= given
        _keep(cfg.app_data, kept)
        cfg = config.load()
    # With no dump known the library is empty, and the app asks for the folders.
    raise SystemExit(play.play(_with_dumps(cfg, kept), None))


def _folders(args: argparse.Namespace) -> None:
    cfg = config.load()
    kept = _kept(args.user)
    if args.kept:
        # One line: the folder, or an empty one when none is set.
        print(kept.get(args.kept, ""))
        return
    if args.roms:
        found = _with_dumps(cfg, {"roms": str(args.roms)})
        if not found.roms:
            names = " or ".join(play.TITLES.values())
            setup.refuse(source_check.Refused(f"No dump of {names} is in {args.roms}\n"
                                              "Choose the folder that holds your .3ds or .cci files"))
        kept["roms"] = str(args.roms)
        play.write_library(found, args.user)
    if args.mods:
        kept["mods"] = str(args.mods)
    _keep(args.user, kept)


def _setup(args: argparse.Namespace) -> None:
    setup.setup(config.load(), args.rom, args.user)


def main() -> None:
    parser = argparse.ArgumentParser(prog="z3d")
    commands = parser.add_subparsers(required=True)
    app = commands.add_parser("play", help="open the title picker")
    app.add_argument("--roms", type=Path, help="the folder of your 3DS dumps, instead of choosing it in the window")
    app.add_argument("--mods", type=Path, help="the folder of your mod downloads, instead of choosing it in the window")
    app.set_defaults(run=_play)
    taken = commands.add_parser("folders", help="what the picker runs to take a folder you chose")
    taken.add_argument("--user", type=Path, required=True)
    taken.add_argument("--roms", type=Path)
    taken.add_argument("--mods", type=Path)
    taken.add_argument("--kept", choices=("roms", "mods"), help="print that folder as it is set now")
    taken.set_defaults(run=_folders)
    made = commands.add_parser("setup", help="what the picker runs to set a game up")
    made.add_argument("--user", type=Path, required=True)
    made.add_argument("rom", type=Path)
    made.set_defaults(run=_setup)
    args = parser.parse_args()
    args.run(args)


if __name__ == "__main__":
    main()
