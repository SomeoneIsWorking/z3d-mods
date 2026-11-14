"""The z3d package installed on another machine (`ssh <host>`) for the player: ~/Games/z3d there is
the package (package.py) with the player's data in `user/` inside it. The host's own files serve
it: the dumps in its ~/ROM/3DS, the player's mod downloads in `user/mods`; its picker sets each
game up from them. What is in `user/` stays, and is copied aside first. A menu and a Desktop
entry open the title picker.
"""

from __future__ import annotations

import json
import shlex
import subprocess
import time
from pathlib import Path

from z3dmods import app, config, package, play, shortcut
from z3dmods.config import Config

FOLDER = "Games/z3d"
BACKUPS = "Games/z3d-backups"
ROMS = "ROM/3DS"
SAVES = app.COPIED
# What a backup leaves out of user/: the sets and the mod downloads, which setup makes again.
DERIVED = ("user/Azahar/load", "user/mods", "user/Azahar/shaders", "user/Azahar/log")


def _ssh(host: str, command: str) -> str:
    return subprocess.run(["ssh", host, command], capture_output=True, text=True, check=True).stdout.strip()


def _rsync(sources: list[Path | str], host: str, target: Path, *flags: str) -> None:
    subprocess.run(["rsync", "-a", *flags, *map(str, sources), f"{host}:{shlex.quote(str(target))}"], check=True)


def install(cfg: Config, host: str) -> list[Path]:
    """Builds the package, copies it to `host` and returns the entries written there."""
    home = Path(_ssh(host, "echo $HOME"))
    root = home / FOLDER
    user = root / "user"
    built = package.build(cfg, package.folder(cfg))
    backup = home / BACKUPS / f"user-{time.strftime('%Y%m%d-%H%M%S')}.tar"
    excluded = " ".join(f"--exclude={shlex.quote(name)}" for name in DERIVED)
    _ssh(host, f"mkdir -p {shlex.quote(str(user / 'Azahar'))} {shlex.quote(str(backup.parent))} "
               "~/.local/share/applications ~/.local/share/icons/hicolor/scalable/apps && "
               f"tar -C {shlex.quote(str(root))} {excluded} -cf {shlex.quote(str(backup))} user")
    _rsync([f"{built}/"], host, root, "--delete", "--exclude=/user")
    present = _ssh(host, f"ls {shlex.quote(str(user / 'Azahar'))} {shlex.quote(str(user))}").split()
    saves = cfg.app_data / "Azahar"
    for name in SAVES:
        if name not in present and (saves / name).exists():
            _rsync([saves / name], host, user / "Azahar")
    staging = cfg.repo / "build" / "host-install" / host
    staging.mkdir(parents=True, exist_ok=True)
    if config.FOLDERS not in present:
        folders = staging / config.FOLDERS
        folders.write_text(json.dumps({"roms": str(home / ROMS)}, indent=2) + "\n")
        _rsync([folders], host, user / folders.name)
    entry = staging / f"{play.DESKTOP_ID}.desktop"
    entry.write_text(shortcut.desktop_entry(shlex.quote(str(root / "z3d")), root))
    entry.chmod(0o755)
    _rsync([built / "z3d.svg"], host, home / ".local/share/icons/hicolor/scalable/apps/z3d.svg")
    written = [home / ".local/share/applications" / entry.name, home / "Desktop" / entry.name]
    for path in written:
        _rsync([entry], host, path)
    return [*written, backup]
