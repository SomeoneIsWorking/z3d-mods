"""The z3d package for Linux: one folder that plays and sets games up with nothing else installed
but the desktop's own libraries (docs/plans/packed-app.md).

    z3d                 the launcher: `python/bin/python3 -I -m z3dmods.packed play`
    AppRun, z3d.desktop what makes the folder an AppDir; `appimage` packs it into one file
    bin/z3d, lib/       the app and every library it links but the C and C++ runtimes, built
                        in a container of an older distribution (build_container.py)
    python/             a relocatable CPython (uv's), with the locked wheels in its site-packages
    tools/z3dmods/      these tools; the app runs `z3dmods.packed folders` and `setup`
    sets/, patch/       the sets and each patch's tables (options, HUD, overlay)
    prebuilt/<game>/    our patch built for the set's base, and the decomp's names the tools read
    prebuilt/glyphs/    port-assets' pad glyphs drawn for the menu textures
    assets/, fonts/     port-assets' glyphs and key typeface, the picker's font
    SOURCE.txt          the commit of every checkout the build was made from (the GPL's source)
    licenses/           the licence of everything in it that is not ours

Nothing third-party that is not ours to give is in it: no game data, no mod. The player's data
is `user/` inside the folder when that exists, else their data folder (config.py); an AppImage
is read-only, so it always uses the data folder.
"""

from __future__ import annotations

import json
import re
import shutil
import subprocess
from pathlib import Path

from z3dmods import (
    app,
    build_container,
    config,
    glyphs,
    patch_build,
    play,
    shortcut,
    source_check,
    sources,
    symbols,
)
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
# What python-build-standalone's sysconfig names as its prefix, where uv writes the folder it
# installed into.
PYTHON_PREFIX = "/install"
APPIMAGE = "z3d-x86_64.AppImage"
LAUNCHER = """#!/bin/sh
# z3d: opens the title picker, which asks for your folders on its first run.
here=$(dirname "$(readlink -f "$0")")
exec "$here/python/bin/python3" -I -m z3dmods.packed play "$@"
"""
SOURCE = """z3d links Azahar's core (GPL-2.0-or-later), so this build as a whole is under the GPL
(licenses/azahar/license.txt). Our own files are MIT (LICENSE). Its source, each at the commit
this build was made from:
  https://github.com/SomeoneIsWorking/z3d-mods at {revision}
{sources}
azahar is the core, with its submodules; lucent, setup-ui and RmlUi are built into bin/z3d;
port-assets is assets/ and what prebuilt/glyphs/ is drawn from.
lib/ holds SDL3, SDL3_ttf and SDL3_image built from the release tarballs that
packaging/Containerfile names, and Ubuntu 24.04's own builds of the rest (apt source <package>).
"""
LICENSES = """The licences of what this package holds that is not ours.

azahar/             the core, lib/libazahar_core.so: license.txt (GPL-2.0), and under externals/
                    the libraries its source tree carries, of which the core builds in a part
lucent/, setup-ui/, RmlUi/
                    built into bin/z3d
SDL3/, SDL3_ttf/, SDL3_image/
                    lib/libSDL3*.so
ubuntu/<package>    the other libraries in lib/, each Ubuntu 24.04 package's copyright file
python/             python/: CPython (also python/lib/python*/LICENSE.txt); the wheels keep
                    theirs in python/lib/python*/site-packages/*.dist-info
appimage-runtime    the AppImage's own first bytes: github.com/AppImage/type2-runtime
                    (MIT), release {runtime}
assets/LICENSE      port-assets' glyphs (MIT), which prebuilt/glyphs/ is drawn from
assets/fonts/OFL.txt, fonts/OFL.txt
                    the Noto Sans typefaces (SIL Open Font License 1.1)
"""
# How a licence file is named in a source tree.
LICENSE_NAMES = ("license", "licence", "copying", "copyright", "notice")


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
    # uv runs the interpreter to ask what it is, which leaves bytecode naming this folder.
    for cache in list((out / "python").rglob("__pycache__")):
        shutil.rmtree(cache)
    # The tools stay beside the sets they read (config.REPO is two folders above them).
    (site / "z3d.pth").write_text("../../../../tools\n")
    for built in (out / "python" / "lib").glob("python*/_sysconfigdata_*.py"):
        built.write_text(built.read_text().replace(str(installed), PYTHON_PREFIX))
    return interpreter


def _libraries(cfg: Config, binary: Path, core: Path, out: Path, licenses: Path) -> None:
    """The libraries `binary` links in the build container, copied out of it into `out`, and
    their licences into `licenses`: the ones built into the image as they installed theirs,
    the distribution's by the package that owns the file."""
    out.mkdir()
    (licenses / "ubuntu").mkdir(parents=True)
    shutil.copyfile(core, out / core.name)
    linked = []
    for line in build_container.output(cfg, ["ldd", str(binary)]).splitlines():
        name, _, rest = line.strip().partition(" => ")
        if not name.startswith((*DESKTOP_LIBRARIES, core.stem)) and rest.startswith("/"):
            linked.append(rest.split()[0])
    build_container.run(cfg, ["cp", "-L", *linked, str(out)])
    build_container.run(cfg, ["cp", "-r", "/usr/local/share/licenses/.", str(licenses)])
    packaged = [path for path in linked if not path.startswith("/usr/local/")]
    owners = build_container.output(cfg, ["sh", "-c", 'for file; do dpkg -S "$(readlink -f "$file")"; done', "sh",
                                          *packaged])
    for owner in sorted({line.split(":")[0] for line in owners.splitlines()}):
        build_container.run(cfg, ["cp", f"/usr/share/doc/{owner}/copyright", str(licenses / "ubuntu" / owner)])


def _license_files(folder: Path) -> list[Path]:
    return sorted(p for p in folder.iterdir() if p.is_file() and p.name.lower().startswith(LICENSE_NAMES)
                  and p.suffix.lower() in ("", ".txt", ".md", ".gplv2", ".gplv3"))


def _licenses(cfg: Config, out: Path, python: Path) -> None:
    """The licences of the sources the build was made from, and the package's list of them."""
    for source in sources.SOURCES:
        folder = source.folder(cfg)
        if folder == cfg.port_assets:
            continue
        (out / source.name).mkdir()
        for file in _license_files(folder):
            shutil.copyfile(file, out / source.name / file.name)
    externals = app.azahar_source(cfg) / "externals"
    for folder in sorted(p for pattern in ("*", "*/*") for p in externals.glob(pattern) if p.is_dir()):
        for file in _license_files(folder):
            kept = out / "azahar" / "externals" / folder.relative_to(externals) / file.name
            kept.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(file, kept)
    (out / "python").mkdir()
    shutil.copyfile(next((python / "lib").glob("python*/LICENSE.txt")), out / "python" / "LICENSE.txt")
    (out / "README.txt").write_text(LICENSES.format(runtime=sources.RUNTIME_RELEASE))


def _glibc(binary: Path) -> tuple[int, ...]:
    """The newest glibc version whose symbols the ELF file `binary` needs."""
    versions = set(re.findall(r"GLIBC_(\d+(?:\.\d+)+)", _run("objdump", "-T", str(binary))))
    return max((tuple(map(int, version.split("."))) for version in versions), default=(0,))


def _check_floor(out: Path) -> None:
    """Refuses a package holding a program or library that needs a newer glibc than the build
    container's, which is the oldest desktop the package says it runs on."""
    newer = []
    for path in sorted(p for p in out.rglob("*") if p.is_file() and not p.is_symlink()):
        with path.open("rb") as file:
            if file.read(4) != b"\x7fELF":
                continue
        needs = _glibc(path)
        if needs > build_container.GLIBC_FLOOR:
            newer.append(f"{path.relative_to(out)} (glibc {'.'.join(map(str, needs))})")
    if newer:
        floor = ".".join(map(str, build_container.GLIBC_FLOOR))
        raise RuntimeError(f"these need a glibc newer than {floor}: {', '.join(newer)}")


def _check_private(cfg: Config, out: Path) -> None:
    """Refuses a package that names this machine: a file holding the home folder's path, the
    dump folder's or a download's, or bytecode, which records where it was compiled."""
    private = [str(Path.home()).encode(), *(str(rom.parent).encode() for rom in cfg.roms.values()),
               str(cfg.mod_sources).encode()]
    found = []
    for path in sorted(p for p in out.rglob("*") if p.is_file() and not p.is_symlink()):
        if path.suffix == ".pyc" or any(word in path.read_bytes() for word in private):
            found.append(str(path.relative_to(out)))
    if found:
        raise RuntimeError(f"these name this machine's folders: {', '.join(found)}")


def _runtime(cfg: Config) -> Path:
    runtime = sources.runtime(cfg)
    if not runtime.exists():
        raise RuntimeError(f"the AppImage runtime is missing: uv run --frozen z3dmods sources fetches {runtime}")
    if source_check.digest(runtime) != sources.RUNTIME_SHA256:
        raise RuntimeError(f"{runtime} is not release {sources.RUNTIME_RELEASE}'s runtime")
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
    binary = app.current_binary(cfg, build_container.BUILDER)
    shutil.copy2(binary, out / "bin" / binary.name)
    core = next(app.core_prefix(cfg, build_container.BUILDER).glob("lib*/libazahar_core.so"))
    _libraries(cfg, binary, core, out / "lib", out / "licenses")
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
    shutil.copyfile(cfg.port_assets / "fonts" / "OFL.txt", out / "fonts" / "OFL.txt")
    shutil.copyfile(cfg.repo / "app" / "icon" / "z3d.svg", out / "z3d.svg")
    shutil.copyfile(cfg.repo / "LICENSE", out / "LICENSE")
    revision = _run("git", "-C", str(cfg.repo), "rev-parse", "HEAD").strip()
    built_from = "\n".join(f"  {source.url} at {sources.built_from(source.folder(cfg))}" for source in sources.SOURCES)
    (out / "SOURCE.txt").write_text(SOURCE.format(revision=sources.built_from(cfg.repo), sources=built_from))
    launcher = out / "z3d"
    launcher.write_text(LAUNCHER)
    launcher.chmod(0o755)
    # What makes the folder an AppDir: its entry point, desktop entry and icon (z3d.svg above).
    (out / "AppRun").symlink_to(launcher.name)
    (out / f"{play.DESKTOP_ID}.desktop").write_text(shortcut.desktop_entry(launcher.name))
    interpreter = _python(out)
    _licenses(cfg, out / "licenses", interpreter.parent.parent)
    _check_floor(out)
    # The mark comes last: a folder with it is a whole package.
    (out / config.PACKAGE_MARK).write_text(json.dumps({"revision": revision, "azahar": app.AZAHAR_REVISION}) + "\n")
    _run(str(interpreter), "-I", "-B", "-c", "import z3dmods.packed")
    _check_private(cfg, out)
    return out
