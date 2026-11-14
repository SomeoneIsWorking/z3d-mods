"""A game's set made for the z3d app from the player's own dump: what the app's title picker
runs when a game's set is missing from its user folder (app/src/set_install.hpp). It prints one
line per stage, which the picker shows.
"""

from __future__ import annotations

import sys
from dataclasses import replace
from pathlib import Path

from z3dmods import ctr, install, play
from z3dmods.config import Config
from z3dmods.install import GameSet


def game_of(cfg: Config, rom: Path) -> GameSet:
    """The set made for the title `rom` holds."""
    title_id = f"{ctr.open_cci(rom).program_id:016X}"
    for name in play.TITLES:
        game = GameSet.named(cfg.repo, name)
        if game.title_id == title_id:
            return game
    raise RuntimeError(f"{rom.name} is title {title_id}, which no set is made for")


def setup(cfg: Config, rom: Path, user: Path) -> Path:
    """Installs the set of `rom`'s game into the app's user folder `user`, reading that dump."""
    # The picker reads each line as it is printed.
    sys.stdout.reconfigure(line_buffering=True)
    print("Reading your game")
    game = game_of(cfg, rom)
    data = user / "Azahar"
    # The set's Azahar settings are standalone Azahar's; the app applies its own.
    target = replace(cfg, roms={game.title_id: rom}, azahar_data=data, azahar_config=data / "config")
    print("Building the mods from your game: this takes a few minutes")
    mods = install.install(target, replace(game, azahar_global={}))
    print("Done")
    return mods
