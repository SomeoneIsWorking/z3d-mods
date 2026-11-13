"""The z3d app (app/) as the tools see it: its build and the user folder the core reads.

A user folder holds Azahar/ in the core's layout: copies of the player's nand, sdmc and sysdata, and
a link to the installed load/ folder (mods and textures, which the core only reads).
"""

from __future__ import annotations

import shutil
import subprocess
from pathlib import Path

from z3dmods.config import Config

COPIED = ("nand", "sdmc", "sysdata")
LINKED = ("load",)


def binary(cfg: Config) -> Path:
    """Builds the app only when no build exists; an existing build is used as it is."""
    if not cfg.app_binary.exists():
        tree = cfg.app_binary.parent
        subprocess.run(["cmake", "-S", str(cfg.repo / "app"), "-B", str(tree), "-G", "Ninja",
                        "-DCMAKE_CXX_COMPILER=clang++", "-DCMAKE_BUILD_TYPE=Release"], check=True)
        subprocess.run(["ninja", "-C", str(tree)], check=True)
    return cfg.app_binary


def seed_user(cfg: Config, user: Path, fresh: bool) -> None:
    """`fresh` replaces the folder with new copies; otherwise only what is missing is added."""
    target = user / "Azahar"
    if fresh and target.exists():
        shutil.rmtree(target)
    target.mkdir(parents=True, exist_ok=True)
    for name in COPIED:
        source = cfg.azahar_data / name
        if source.exists() and not (target / name).exists():
            shutil.copytree(source, target / name, symlinks=True)
    for name in LINKED:
        source = cfg.azahar_data / name
        if source.exists() and not (target / name).exists():
            (target / name).symlink_to(source, target_is_directory=True)


def launch_args(cfg: Config, rom: Path, user: Path, options: dict[str, str]) -> list[str]:
    if cfg.libretro_core is None:
        raise RuntimeError('set "libretro_core" in env-assets/local.json to the built azahar_libretro.so')
    args = [str(binary(cfg)), "--core", str(cfg.libretro_core), "--rom", str(rom), "--user", str(user)]
    for key, value in options.items():
        args += ["--option", f"{key}={value}"]
    return args
