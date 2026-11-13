"""Play in the z3d app with the installed mods, in a window on the player's session: its title
picker over every set, or one game directly.

The app keeps its own saves in <app_data>/Azahar, seeded once from the player's Azahar; standalone
Azahar's saves are not touched.
"""

from __future__ import annotations

import json
import os
import subprocess
from pathlib import Path

from z3dmods import app
from z3dmods.config import Config
from z3dmods.install import GameSet

RESOLUTION = 3
TITLES = {"oot3d": "Ocarina of Time 3D", "mm3d": "Majora's Mask 3D"}
# The desktop entry (shortcut.py) and window app id, so the window gets its icon.
DESKTOP_ID = "z3d"


def write_library(cfg: Config, user: Path) -> Path:
    """The picker's games (app/src/game_library.hpp), beside the saves in `user`."""
    entries = []
    for name in TITLES:
        game = GameSet.named(cfg.repo, name)
        entries.append({"rom": str(cfg.rom(game.title_id)), "layout": game.screen_layout,
                        "textures": app.uses_textures(game)})
    path = user / "library.json"
    path.write_text(json.dumps(entries, indent=2) + "\n")
    return path


def launch_args(cfg: Config, name: str | None, user: Path) -> list[str]:
    """The windowed app as the player runs it on the user folder `user`: game `name`, or the picker."""
    if name is None:
        return [str(app.binary(cfg)), "--library", str(write_library(cfg, user)), "--font", str(cfg.ui_font()),
                "--user", str(user), "--resolution", str(RESOLUTION), "--app-id", DESKTOP_ID]
    return app.launch_args(cfg, GameSet.named(cfg.repo, name), user, RESOLUTION) + [
        "--title", TITLES[name], "--app-id", DESKTOP_ID]


def play(cfg: Config, name: str | None) -> int:
    app.seed_user(cfg, cfg.app_data, fresh=False)
    args = launch_args(cfg, name, cfg.app_data)
    return subprocess.run(args, env=dict(os.environ, **cfg.session_env), check=False).returncode
