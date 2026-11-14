"""The desktop shortcut for this machine (never committed): the z3d app's title picker, launched
through `z3dmods play` on the newest build (app.binary builds only when none exists)."""

from __future__ import annotations

import shutil
from pathlib import Path

from z3dmods import play
from z3dmods.config import Config

# Per-game entries from before the picker.
RETIRED = ("z3d-oot3d.desktop", "z3d-mm3d.desktop")


def folders() -> list[Path]:
    return [Path.home() / ".local/share/applications", Path.home() / "Desktop"]


def exec_line(cfg: Config) -> str:
    return " ".join(f'"{part}"' for part in (*cfg.tool_command, "play"))


def desktop_entry(exec_: str, folder: Path | None = None) -> str:
    """The entry that opens the title picker; `folder` is its working directory."""
    return ("[Desktop Entry]\nType=Application\nName=z3d\n"
            "Comment=Ocarina of Time 3D and Majora's Mask 3D with their mods\n"
            f"Exec={exec_}\n" + (f"Path={folder}\n" if folder else "") +
            "Icon=z3d\nTerminal=false\nCategories=Game;\n")


def install(cfg: Config) -> list[Path]:
    icons = Path.home() / ".local/share/icons/hicolor/scalable/apps"
    icons.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(cfg.repo / "app" / "icon" / "z3d.svg", icons / "z3d.svg")
    entry = desktop_entry(exec_line(cfg))
    written = []
    folders()[0].mkdir(parents=True, exist_ok=True)
    for folder in folders():
        if not folder.is_dir():
            continue
        for old in RETIRED:
            (folder / old).unlink(missing_ok=True)
        path = folder / f"{play.DESKTOP_ID}.desktop"
        path.write_text(entry)
        path.chmod(0o755)
        written.append(path)
    return written


def desktop_exec(path: Path) -> str:
    """The Exec line of an installed entry, as the desktop runs it."""
    for line in path.read_text().splitlines():
        if line.startswith("Exec="):
            return line.removeprefix("Exec=")
    raise RuntimeError(f"{path} has no Exec line")
