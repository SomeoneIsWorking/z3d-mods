"""Play a game in the z3d app with the installed mods, in a window on the player's session.

The app keeps its own saves in <app_data>/Azahar, seeded once from the player's Azahar; standalone
Azahar's saves are not touched.
"""

from __future__ import annotations

import os
import subprocess

from z3dmods import app, core_options
from z3dmods.config import Config
from z3dmods.install import GameSet

RESOLUTION = 3
TITLES = {"oot3d": "Ocarina of Time 3D", "mm3d": "Majora's Mask 3D"}


def desktop_id(name: str) -> str:
    """The desktop entry (shortcut.py) and window app id for a game, so the window gets its icon."""
    return f"z3d-{name}"


def play(cfg: Config, name: str, game: GameSet) -> int:
    app.seed_user(cfg, cfg.app_data, fresh=False)
    args = app.launch_args(cfg, cfg.rom(game.title_id), cfg.app_data, core_options.for_set(game, RESOLUTION))
    args += ["--title", TITLES[name], "--app-id", desktop_id(name)]
    return subprocess.run(args, env=dict(os.environ, **cfg.session_env), check=False).returncode
