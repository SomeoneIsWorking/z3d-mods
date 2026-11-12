"""The one reader of the environment and of the gitignored local settings."""

from __future__ import annotations

import json
import os
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
    # The SomeoneIsWorking/azahar libretro core the harness loads (local.json "libretro_core").
    libretro_core: Path | None

    def rom(self, title_id: str) -> Path:
        if title_id not in self.roms:
            raise KeyError(f"no ROM for {title_id} in env-assets/local.json")
        return self.roms[title_id]

    def mods_dir(self, title_id: str) -> Path:
        return self.azahar_data / "load" / "mods" / title_id

    def textures_dir(self, title_id: str) -> Path:
        return self.azahar_data / "load" / "textures" / title_id


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
        libretro_core=Path(local["libretro_core"]) if "libretro_core" in local else None,
    )
