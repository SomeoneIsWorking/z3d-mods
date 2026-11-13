"""Play a game in the z3d app with the installed mods, in a window on the player's session.

The app keeps its own saves in <app_data>/Azahar, seeded once from the player's Azahar; standalone
Azahar's saves are not touched.
"""

from __future__ import annotations

import os
import subprocess
from pathlib import Path

from z3dmods import app
from z3dmods.config import Config
from z3dmods.install import GameSet

RESOLUTION = 3
TITLES = {"oot3d": "Ocarina of Time 3D", "mm3d": "Majora's Mask 3D"}


def desktop_id(name: str) -> str:
    """The desktop entry (shortcut.py) and window app id for a game, so the window gets its icon."""
    return f"z3d-{name}"


def launch_args(cfg: Config, name: str, game: GameSet, user: Path) -> list[str]:
    """The windowed app as the player runs it, on the user folder `user`."""
    return app.launch_args(cfg, game, user, RESOLUTION) + ["--title", TITLES[name], "--app-id", desktop_id(name)]


def play(cfg: Config, name: str, game: GameSet) -> int:
    app.seed_user(cfg, cfg.app_data, fresh=False)
    args = launch_args(cfg, name, game, cfg.app_data)
    return subprocess.run(args, env=dict(os.environ, **cfg.session_env), check=False).returncode
