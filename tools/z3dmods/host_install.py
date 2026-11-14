"""The z3d app installed on another machine (`ssh <host>`) for the player: ~/Games/z3d there holds the
newest app build, its core library, the UI font, a copy of the installed sets (load/) and the app's
saves, copied once from this machine's. The ROMs stay in the host's ~/ROM/3DS under the same names.
A menu and a Desktop entry open the title picker.
"""

from __future__ import annotations

import shlex
import subprocess
from pathlib import Path

from z3dmods import app, play, shortcut
from z3dmods.config import Config

FOLDER = "Games/z3d"
ROMS = "ROM/3DS"
SAVES = app.COPIED


def _ssh(host: str, command: str) -> str:
    return subprocess.run(["ssh", host, command], capture_output=True, text=True, check=True).stdout.strip()


def _rsync(sources: list[Path], host: str, target: Path, *flags: str) -> None:
    subprocess.run(["rsync", "-a", *flags, *map(str, sources), f"{host}:{shlex.quote(str(target))}"], check=True)


def install(cfg: Config, host: str) -> list[Path]:
    """Copies the install to `host` and returns the entries written there."""
    home = Path(_ssh(host, "echo $HOME"))
    root = home / FOLDER
    user = root / "user"
    binary = app.binary(cfg)
    core = next(app.core_prefix(cfg).glob("lib*/libazahar_core.so"))
    _ssh(host, f"mkdir -p {shlex.quote(str(root / 'lib'))} {shlex.quote(str(root / 'fonts'))} {shlex.quote(str(root / 'assets'))} "
               f"{shlex.quote(str(user / 'Azahar'))} ~/.local/share/applications "
               "~/.local/share/icons/hicolor/scalable/apps")
    _rsync([binary], host, root / binary.name)
    _rsync([core], host, root / "lib" / core.name)
    _rsync([cfg.ui_font()], host, root / "fonts" / cfg.ui_font().name)
    # What the play HUD draws from: port-assets' sets and its key typeface.
    _rsync([cfg.port_assets / "sets", cfg.port_assets / "fonts"], host, root / "assets", "--delete")
    _rsync([cfg.azahar_data / "load"], host, user / "Azahar", "--delete", "--copy-links")
    present = _ssh(host, f"ls {shlex.quote(str(user / 'Azahar'))}").split()
    saves = cfg.app_data / "Azahar"
    for name in SAVES:
        if name not in present and (saves / name).exists():
            _rsync([saves / name], host, user / "Azahar")

    staging = cfg.repo / "build" / "host-install" / host
    staging.mkdir(parents=True, exist_ok=True)
    library = play.write_library(cfg, staging, home / ROMS)
    _rsync([library], host, user / library.name)
    args = play.picker_args(root / binary.name, user / library.name, root / "fonts" / cfg.ui_font().name,
                            root / "assets", user)
    exec_ = f"env LD_LIBRARY_PATH={root / 'lib'} " + " ".join(args)
    entry = staging / f"{play.DESKTOP_ID}.desktop"
    entry.write_text(shortcut.desktop_entry(exec_, root))
    entry.chmod(0o755)
    _rsync([cfg.repo / "app" / "icon" / "z3d.svg"], host, home / ".local/share/icons/hicolor/scalable/apps/z3d.svg")
    written = [home / ".local/share/applications" / entry.name, home / "Desktop" / entry.name]
    for path in written:
        _rsync([entry], host, path)
    return written
