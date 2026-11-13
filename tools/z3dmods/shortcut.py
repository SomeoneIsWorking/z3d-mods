"""Desktop shortcuts for this machine (never committed): one per game, launching `z3dmods play`."""

from __future__ import annotations

import shutil
from pathlib import Path

from z3dmods import play
from z3dmods.config import Config

def install(cfg: Config) -> list[Path]:
    uv = shutil.which("uv")
    if uv is None:
        raise RuntimeError("uv is not on PATH")
    icons = Path.home() / ".local/share/icons/hicolor/scalable/apps"
    icons.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(cfg.repo / "app" / "icon" / "z3d.svg", icons / "z3d.svg")
    applications = Path.home() / ".local/share/applications"
    applications.mkdir(parents=True, exist_ok=True)
    desktop = Path.home() / "Desktop"
    written = []
    for game, title in play.TITLES.items():
        entry = (f"[Desktop Entry]\nType=Application\nName={title} (z3d)\n"
                 f"Comment=Play {title} with its mods in the z3d app\n"
                 f'Exec="{uv}" --directory "{cfg.repo}" run --frozen z3dmods play {game}\n'
                 "Icon=z3d\nTerminal=false\nCategories=Game;\n")
        for folder in (applications, desktop):
            if not folder.is_dir():
                continue
            path = folder / f"{play.desktop_id(game)}.desktop"
            path.write_text(entry)
            path.chmod(0o755)
            written.append(path)
    return written
