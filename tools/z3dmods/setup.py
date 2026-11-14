"""A game's set made for the z3d app from the player's own dump: what the app's title picker
runs when a game's set is missing from its user folder (app/src/set_install.hpp). It prints one
line per stage, which the picker shows; a refusal's lines start with REFUSAL (`refuse`), and
the picker shows those when the setup fails.
"""

from __future__ import annotations

import sys
from dataclasses import replace
from pathlib import Path

from z3dmods import ctr, install, play, source_check
from z3dmods.config import Config
from z3dmods.install import GameSet


# What a line of a refusal starts with (app/src/tool_process.cpp kRefusal).
REFUSAL = "! "


def game_of(cfg: Config, rom: Path) -> GameSet:
    """The set made for the title `rom` holds."""
    try:
        title_id = f"{ctr.open_cci(rom).program_id:016X}"
    except (ValueError, OSError) as error:
        raise source_check.Refused(f"{rom.name} is not a decrypted 3DS dump\n{error}") from error
    for name in play.TITLES:
        game = GameSet.named(cfg.repo, name)
        if game.title_id == title_id:
            return game
    raise source_check.Refused(f"{rom.name} is title {title_id}, which this setup has no mods for")


def setup(cfg: Config, rom: Path, user: Path) -> None:
    """Installs the set of `rom`'s game into the app's user folder `user`, reading that dump and
    the player's mod downloads. A refusal is printed
    for the picker and ends the program with 1."""
    # The picker reads each line as it is printed.
    sys.stdout.reconfigure(line_buffering=True)
    print("Reading your game")
    try:
        game = game_of(cfg, rom)
        data = user / "Azahar"
        # The set's Azahar settings are standalone Azahar's; the app applies its own.
        target = replace(cfg, roms={game.title_id: rom}, azahar_data=data, azahar_config=data / "config")
        print("Building the mods from your game: this takes a few minutes")
        install.install(target, replace(game, azahar_global={}))
    except source_check.Refused as refusal:
        refuse(refusal)
    print("Done")


def refuse(refusal: source_check.Refused) -> None:
    """Prints `refusal` for the app (app/src/tool_process.cpp) and ends the program with 1."""
    for line in str(refusal).splitlines():
        print(REFUSAL + line)
    raise SystemExit(1) from refusal
