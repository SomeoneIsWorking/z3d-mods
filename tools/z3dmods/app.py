"""The z3d app (app/) as the tools see it: its build, the user folder the core reads, its flags.

The app builds against the SomeoneIsWorking/azahar core (local.json "azahar_source", checked out at
AZAHAR_REVISION), installed once per revision and toolchain into a prefix under the dependency
folder and found with find_package(AzaharCore). A user folder holds Azahar/ in the core's layout:
copies of the player's nand, sdmc and sysdata, and a link to the installed load/ folder (mods and
textures, which the core only reads).
"""

from __future__ import annotations

import shutil
import subprocess
from pathlib import Path

from z3dmods.config import Config
from z3dmods.install import GameSet

# The fork's z3d-harness branch the app is built against.
AZAHAR_REVISION = "67f489de2"
TOOLCHAIN = "clang"
COMPILERS = ["-DCMAKE_C_COMPILER=clang", "-DCMAKE_CXX_COMPILER=clang++"]
AZAHAR_OPTIONS = {
    "ENABLE_QT": "OFF", "ENABLE_SDL2": "OFF", "ENABLE_LIBRETRO": "OFF", "ENABLE_TESTS": "OFF",
    "ENABLE_ROOM": "OFF", "ENABLE_WEB_SERVICE": "OFF", "ENABLE_SCRIPTING": "OFF",
    "ENABLE_GDBSTUB": "OFF", "ENABLE_OPENGL": "OFF", "ENABLE_SOFTWARE_RENDERER": "OFF",
    "ENABLE_OPENAL": "OFF", "ENABLE_LIBUSB": "OFF",
    "AZAHAR_CORE_PACKAGE": "ON", "CMAKE_POSITION_INDEPENDENT_CODE": "ON",
}
COPIED = ("nand", "sdmc", "sysdata")
LINKED = ("load",)


def azahar_source(cfg: Config) -> Path:
    if cfg.azahar_source is None:
        raise RuntimeError('set "azahar_source" in env-assets/local.json to the SomeoneIsWorking/azahar checkout')
    head = subprocess.run(["git", "-C", str(cfg.azahar_source), "rev-parse", "HEAD"],
                          capture_output=True, text=True, check=True).stdout.strip()
    pinned = subprocess.run(["git", "-C", str(cfg.azahar_source), "rev-parse", f"{AZAHAR_REVISION}^{{commit}}"],
                            capture_output=True, text=True, check=True).stdout.strip()
    if head != pinned:
        raise RuntimeError(f"{cfg.azahar_source} is at {head[:12]}, the app pins {AZAHAR_REVISION}")
    return cfg.azahar_source


def core_prefix(cfg: Config) -> Path:
    return cfg.dependencies / "azahar-core" / f"{AZAHAR_REVISION}-{TOOLCHAIN}" / "prefix"


def core(cfg: Config) -> Path:
    """Builds and installs the core only when its prefix for this revision does not exist."""
    prefix = core_prefix(cfg)
    if not any(prefix.glob("lib*/cmake/AzaharCore/AzaharCoreConfig.cmake")):
        tree = prefix.parent / "build"
        flags = [f"-D{key}={value}" for key, value in AZAHAR_OPTIONS.items()]
        subprocess.run(["cmake", "-S", str(azahar_source(cfg)), "-B", str(tree), "-G", "Ninja",
                        "-DCMAKE_BUILD_TYPE=Release", *COMPILERS, f"-DCMAKE_INSTALL_PREFIX={prefix}", *flags],
                       check=True)
        subprocess.run(["ninja", "-C", str(tree), "install"], check=True)
    return prefix


def binary(cfg: Config) -> Path:
    """Builds the app only when no build exists; an existing build is used as it is."""
    if not cfg.app_binary.exists():
        tree = cfg.app_binary.parent
        subprocess.run(["cmake", "-S", str(cfg.repo / "app"), "-B", str(tree), "-G", "Ninja",
                        "-DCMAKE_BUILD_TYPE=Release", *COMPILERS, f"-DCMAKE_PREFIX_PATH={core(cfg)}"], check=True)
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


def launch_args(cfg: Config, game: GameSet, user: Path, resolution: int,
                textures: bool | None = None) -> list[str]:
    """`resolution` is the render scale (1 is the 3DS's own 400x240 top screen); `textures`
    overrides the set's custom textures."""
    args = [str(binary(cfg)), "--rom", str(cfg.rom(game.title_id)), "--user", str(user),
            "--layout", game.screen_layout, "--resolution", str(resolution)]
    if (game.textures is not None) if textures is None else textures:
        args.append("--textures")
    return args
