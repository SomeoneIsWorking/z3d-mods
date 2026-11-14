"""The z3d package for Linux: one folder that plays and sets games up with nothing else installed
but the desktop's own libraries (docs/plans/packed-app.md).

    z3d                 the launcher: `python/bin/python3 -I -m z3dmods.packed play`
    bin/z3d, lib/       the app, the core library and SDL3
    python/             a relocatable CPython (uv's), with the locked wheels in its site-packages
    tools/z3dmods/      these tools; the app runs `z3dmods.packed setup` as its installer
    sets/, patch/       the sets and each patch's tables (options, HUD, overlay)
    prebuilt/<game>/    our patch built for the set's base, and the decomp's names the tools read
    assets/, fonts/     port-assets' glyphs and key typeface, the picker's font

Nothing third-party that is not ours to give is in it: no game data, no mod. The player's data
is `user/` inside the folder when that exists, else their data folder (config.py).
"""

from __future__ import annotations

import json
import shutil
import subprocess
from pathlib import Path

from z3dmods import app, config, patch_build, play, symbols
from z3dmods.config import Config
from z3dmods.install import GameSet

PYTHON = "3.12.16"
# What a relocatable CPython carries that the tools never import.
PYTHON_UNUSED = ("include", "share", "lib/tcl*", "lib/tk*", "lib/itcl*", "lib/thread*", "lib/libtcl*", "lib/libtk*",
                 "lib/python*/test", "lib/python*/tkinter", "lib/python*/idlelib", "lib/python*/ensurepip",
                 "lib/python*/turtledemo", "lib/python*/lib2to3", "lib/python*/config-*", "lib/pkgconfig")
# The app's libraries a desktop may not have; the rest are the desktop's own.
BUNDLED_LIBRARIES = ("libazahar_core", "libSDL3", "libplutosvg", "libplutovg")
LAUNCHER = """#!/bin/sh
# z3d: opens the title picker. --roms <folder> and --mods <folder> name your files, once.
here=$(dirname "$(readlink -f "$0")")
exec "$here/python/bin/python3" -I -m z3dmods.packed play "$@"
"""
SOURCE = """z3d links Azahar's core (GPL-2.0-or-later), so this build as a whole is under the GPL.
Its source:
  https://github.com/SomeoneIsWorking/z3d-mods at {revision} (our own files are MIT: LICENSE)
  https://github.com/SomeoneIsWorking/azahar at {azahar}
"""


def _run(*command: str) -> str:
    return subprocess.run(command, check=True, capture_output=True, text=True).stdout


def _python(out: Path) -> Path:
    """A relocatable CPython in `out`/python with the locked wheels; returns its interpreter."""
    staging = out / "python-install"
    _run("uv", "python", "install", PYTHON, "--no-bin", "--install-dir", str(staging))
    (installed,) = (p for p in staging.glob("cpython-*") if not p.is_symlink())
    installed.rename(out / "python")
    shutil.rmtree(staging)
    for pattern in PYTHON_UNUSED:
        for path in (out / "python").glob(pattern):
            shutil.rmtree(path) if path.is_dir() else path.unlink()
    interpreter = out / "python" / "bin" / "python3"
    requirements = out / "requirements.txt"
    _run("uv", "export", "--frozen", "--no-dev", "--no-emit-project", "--directory", str(config.REPO),
         "--format", "requirements-txt", "-o", str(requirements))
    site = next((out / "python" / "lib").glob("python*/site-packages"))
    _run("uv", "pip", "install", "--python", str(interpreter), "--target", str(site), "--require-hashes",
         "-r", str(requirements))
    requirements.unlink()
    # The tools stay beside the sets they read (config.REPO is two folders above them).
    (site / "z3d.pth").write_text("../../../../tools\n")
    return interpreter


def _libraries(binary: Path, core: Path, out: Path) -> None:
    out.mkdir()
    shutil.copyfile(core, out / core.name)
    needed = _run("ldd", str(binary)).splitlines()
    for line in needed:
        name, _, rest = line.strip().partition(" => ")
        if name.startswith(BUNDLED_LIBRARIES) and not name.startswith(core.stem) and rest.startswith("/"):
            shutil.copyfile(Path(rest.split()[0]).resolve(), out / name)


def build(cfg: Config, out: Path) -> Path:
    """Builds the package folder `out` from this checkout, replacing an earlier one."""
    if out.exists():
        shutil.rmtree(out)
    (out / "bin").mkdir(parents=True)
    binary = app.binary(cfg)
    shutil.copy2(binary, out / "bin" / binary.name)
    _libraries(binary, next(app.core_prefix(cfg).glob("lib*/libazahar_core.so")), out / "lib")
    shutil.copytree(cfg.repo / "tools" / "z3dmods", out / "tools" / "z3dmods",
                    ignore=shutil.ignore_patterns("__pycache__"))
    shutil.copytree(cfg.repo / "sets", out / "sets")
    for name in play.TITLES:
        game = GameSet.named(cfg.repo, name)
        (out / "patch" / name).mkdir(parents=True)
        for table in (cfg.repo / "patch" / name).glob("*.toml"):
            shutil.copyfile(table, out / "patch" / name / table.name)
        built = out / patch_build.PREBUILT / name
        patch_build.save(patch_build.build(cfg.repo, name, game.payload, cfg.repo / "build" / "patch" / name), built)
        symbols.save(symbols.load(cfg.repo, name), built / symbols.PACKAGED)
    for part in ("sets", "fonts"):
        shutil.copytree(cfg.port_assets / part, out / "assets" / part)
    shutil.copyfile(cfg.port_assets / "LICENSE", out / "assets" / "LICENSE")
    (out / "fonts").mkdir()
    shutil.copyfile(cfg.ui_font(), out / "fonts" / cfg.ui_font().name)
    shutil.copyfile(cfg.repo / "app" / "icon" / "z3d.svg", out / "z3d.svg")
    shutil.copyfile(cfg.repo / "LICENSE", out / "LICENSE")
    revision = _run("git", "-C", str(cfg.repo), "rev-parse", "HEAD").strip()
    (out / "SOURCE.txt").write_text(SOURCE.format(revision=revision, azahar=app.AZAHAR_REVISION))
    launcher = out / "z3d"
    launcher.write_text(LAUNCHER)
    launcher.chmod(0o755)
    interpreter = _python(out)
    # The mark comes last: a folder with it is a whole package.
    (out / config.PACKAGE_MARK).write_text(json.dumps({"revision": revision, "azahar": app.AZAHAR_REVISION}) + "\n")
    _run(str(interpreter), "-I", "-c", "import z3dmods.packed")
    return out
