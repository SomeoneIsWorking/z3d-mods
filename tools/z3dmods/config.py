"""The one reader of the environment and of the gitignored local settings."""

from __future__ import annotations

import json
import os
import subprocess
from dataclasses import dataclass
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]


@dataclass(frozen=True)
class Config:
    repo: Path
    env_assets: Path
    azahar_data: Path
    azahar_config: Path
    azahar_appimage: Path
    roms: dict[str, Path]
    # The SomeoneIsWorking/azahar checkout the app builds inside (local.json "azahar_source").
    azahar_source: Path | None
    # The z3d app (app/) and the data it plays from (saves, system files, a link to load/).
    app_binary: Path
    app_data: Path
    # The desktop session's display variables, so a tool started outside it reaches it too.
    session_env: dict[str, str]

    def rom(self, title_id: str) -> Path:
        if title_id not in self.roms:
            raise KeyError(f"no ROM for {title_id} in env-assets/local.json")
        return self.roms[title_id]

    def mods_dir(self, title_id: str) -> Path:
        return self.azahar_data / "load" / "mods" / title_id

    def textures_dir(self, title_id: str) -> Path:
        return self.azahar_data / "load" / "textures" / title_id


SESSION_KEYS = ("WAYLAND_DISPLAY", "DISPLAY", "XAUTHORITY", "XDG_SESSION_TYPE")


def _session_env() -> dict[str, str]:
    if os.environ.get("WAYLAND_DISPLAY") or os.environ.get("DISPLAY"):
        return {key: os.environ[key] for key in SESSION_KEYS if key in os.environ}
    shown = subprocess.run(["systemctl", "--user", "show-environment"], capture_output=True,
                           text=True, check=True).stdout
    found = dict(line.split("=", 1) for line in shown.splitlines() if "=" in line)
    return {key: found[key] for key in SESSION_KEYS if key in found}


def load() -> Config:
    home = Path.home()
    data_home = Path(os.environ.get("XDG_DATA_HOME") or home / ".local/share")
    config_home = Path(os.environ.get("XDG_CONFIG_HOME") or home / ".config")
    env_assets = REPO / "env-assets"
    local = json.loads((env_assets / "local.json").read_text())
    installed = json.loads((data_home / "opensu/emulators/azahar/installed.json").read_text())
    return Config(
        repo=REPO,
        env_assets=env_assets,
        azahar_data=data_home / "azahar-emu",
        azahar_config=config_home / "azahar-emu",
        azahar_appimage=data_home / "opensu/emulators/azahar" / installed["file"],
        roms={tid: Path(path) for tid, path in local["roms"].items()},
        azahar_source=Path(local["azahar_source"]) if "azahar_source" in local else None,
        app_binary=REPO / "build" / "app" / "z3d",
        app_data=data_home / "z3d-mods",
        session_env=_session_env(),
    )
