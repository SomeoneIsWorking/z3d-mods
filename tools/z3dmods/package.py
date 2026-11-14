"""The z3d package for Linux: one folder that plays and sets games up with nothing else installed
but the desktop's own libraries (docs/plans/packed-app.md).

    z3d                 the launcher: `python/bin/python3 -I -m z3dmods.packed play`
    AppRun, z3d.desktop what makes the folder an AppDir; `appimage` packs it into one file
    bin/z3d, lib/       the app and every library it links but the C and C++ runtimes
    python/             a relocatable CPython (uv's), with the locked wheels in its site-packages
    tools/z3dmods/      these tools; the app runs `z3dmods.packed folders` and `setup`
    sets/, patch/       the sets and each patch's tables (options, HUD, overlay)
    prebuilt/<game>/    our patch built for the set's base, and the decomp's names the tools read
    prebuilt/glyphs/    port-assets' pad glyphs drawn for the menu textures
    assets/, fonts/     port-assets' glyphs and key typeface, the picker's font

Nothing third-party that is not ours to give is in it: no game data, no mod. The player's data
is `user/` inside the folder when that exists, else their data folder (config.py); an AppImage
is read-only, so it always uses the data folder.
"""

from __future__ import annotations

import json
import shutil
import subprocess
from pathlib import Path

from z3dmods import app, config, glyphs, patch_build, play, shortcut, source_check, symbols
from z3dmods.config import Config
from z3dmods.install import GameSet

PYTHON = "3.12.16"
# What a relocatable CPython carries that the tools never import.
PYTHON_UNUSED = ("include", "share", "lib/tcl*", "lib/tk*", "lib/itcl*", "lib/thread*", "lib/libtcl*", "lib/libtk*",
                 "lib/python*/test", "lib/python*/tkinter", "lib/python*/idlelib", "lib/python*/ensurepip",
                 "lib/python*/turtledemo", "lib/python*/lib2to3", "lib/python*/config-*", "lib/pkgconfig")
# The libraries the app takes from the desktop: the C and C++ runtimes, which a package cannot
# stand in for. Every other one it links comes along (the font, text and image libraries with
# what they need). Vulkan is not linked: the core opens the desktop's loader.
DESKTOP_LIBRARIES = ("linux-vdso", "ld-linux", "libc.", "libm.", "libgcc_s", "libstdc++")
# The AppImage runtime (github.com/AppImage/type2-runtime, MIT): the program a squashfs image of
# the package folder is appended to.
RUNTIME_RELEASE = "20251108"
RUNTIME_SHA256 = "2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d"
APPIMAGE = "z3d-x86_64.AppImage"
LAUNCHER = """#!/bin/sh
# z3d: opens the title picker, which asks for your folders on its first run.
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
        if not name.startswith((*DESKTOP_LIBRARIES, core.stem)) and rest.startswith("/"):
            shutil.copyfile(Path(rest.split()[0]).resolve(), out / name)


def _runtime(cfg: Config) -> Path:
    runtime = cfg.dependencies / "appimage-runtime" / RUNTIME_RELEASE / "runtime-x86_64"
    if not runtime.exists():
        raise RuntimeError(f"the AppImage runtime is missing: gh release download {RUNTIME_RELEASE} "
                           f"-R AppImage/type2-runtime -p runtime-x86_64 -D {runtime.parent}")
    if source_check.digest(runtime) != RUNTIME_SHA256:
        raise RuntimeError(f"{runtime} is not release {RUNTIME_RELEASE}'s runtime")
    return runtime


def appimage(cfg: Config, built: Path) -> Path:
    """The package folder `built` as one file beside it: the AppImage runtime with the folder
    appended as a squashfs image, which the runtime mounts and whose AppRun it starts."""
    if shutil.which("mksquashfs") is None:
        raise RuntimeError("mksquashfs is missing: sudo dnf install squashfs-tools (Fedora), "
                           "sudo apt install squashfs-tools (Debian/Ubuntu)")
    image = built.with_name(APPIMAGE)
    squashed = built.with_name(APPIMAGE + ".squashfs")
    squashed.unlink(missing_ok=True)
    _run("mksquashfs", str(built), str(squashed), "-root-owned", "-noappend", "-comp", "zstd", "-quiet", "-no-progress")
    with image.open("wb") as out, squashed.open("rb") as body:
        out.write(_runtime(cfg).read_bytes())
        shutil.copyfileobj(body, out)
    squashed.unlink()
    image.chmod(0o755)
    return image


def folder(cfg: Config) -> Path:
    """Where `z3dmods package` makes the package."""
    return cfg.repo / "build" / "package" / "z3d"


def build(cfg: Config, out: Path) -> Path:
    """Builds the package folder `out` from this checkout, replacing an earlier one."""
    if out.exists():
        shutil.rmtree(out)
    (out / "bin").mkdir(parents=True)
    binary = app.current_binary(cfg)
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
        built = out / config.PREBUILT / name
        patch_build.save(patch_build.build(cfg.repo, name, game.payload, cfg.repo / "build" / "patch" / name), built)
        symbols.save(symbols.load(cfg.repo, name), built / symbols.PACKAGED)
    glyphs.save(cfg.glyph_set(), out / config.PREBUILT / glyphs.FOLDER)
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
    # What makes the folder an AppDir: its entry point, desktop entry and icon (z3d.svg above).
    (out / "AppRun").symlink_to(launcher.name)
    (out / f"{play.DESKTOP_ID}.desktop").write_text(shortcut.desktop_entry(launcher.name))
    interpreter = _python(out)
    # The mark comes last: a folder with it is a whole package.
    (out / config.PACKAGE_MARK).write_text(json.dumps({"revision": revision, "azahar": app.AZAHAR_REVISION}) + "\n")
    _run(str(interpreter), "-I", "-c", "import z3dmods.packed")
    return out
