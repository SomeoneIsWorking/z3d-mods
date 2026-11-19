"""The z3d package for macOS (Apple silicon): `z3d.app`, whose Contents/Resources is the package
folder package.py describes, and the zip of it a release carries.

    Contents/MacOS/z3d          the launcher: Resources/python/bin/python3 -I -m z3dmods.packed play
    Contents/Resources/bin/z3d  the app, built on this Mac against Homebrew's libraries
    Contents/Resources/lib/     the core, MoltenVK and every Homebrew library the app and the core
                                link, each named @rpath/<file> so the bundle needs no Homebrew

The core opens Vulkan by name: with no loader installed it opens lib/libMoltenVK.dylib, which
config.app_env puts on the library fallback path. Everything is signed ad hoc.
"""

from __future__ import annotations

import re
import shutil
import subprocess
from pathlib import Path

from z3dmods import app, package, shortcut
from z3dmods.config import Config

ZIP = "z3d-macos-arm64.zip"
# Where Homebrew installs on Apple silicon; a library linked from anywhere else is the system's.
HOMEBREW = "/opt/homebrew"
LAUNCHER = """#!/bin/sh
# z3d: opens the title picker, which asks for your folders on its first run.
here=$(cd "$(dirname "$0")/../Resources" && pwd)
exec "$here/python/bin/python3" -I -m z3dmods.packed play "$@"
"""


def _run(*command: str) -> str:
    return subprocess.run(command, check=True, capture_output=True, text=True).stdout


def _linked(path: Path) -> list[str]:
    """The Homebrew libraries the Mach-O file `path` names, as it names them."""
    lines = _run("otool", "-L", str(path)).splitlines()[1:]
    names = (line.strip().split(" (")[0] for line in lines)
    return [name for name in names if name.startswith(HOMEBREW)]


def _keg(library: str) -> Path | None:
    """The Homebrew keg a library path resolves into (Cellar/<formula>/<version>)."""
    match = re.match(r"(.*/Cellar/[^/]+/[^/]+)/", str(Path(library).resolve()))
    return Path(match.group(1)) if match else None


def _bundle_libraries(binary: Path, core: Path, out: Path, licenses: Path) -> None:
    """Copies `core`, MoltenVK and every Homebrew library they and `binary` need into `out`,
    renames each to @rpath/<file>, points every reference at that name, and copies each
    formula's licence files into `licenses`/homebrew/<formula>."""
    out.mkdir()
    moltenvk = Path(HOMEBREW) / "opt" / "molten-vk" / "lib" / "libMoltenVK.dylib"
    seeds = [core, moltenvk]
    # Each library as the files that link it name it, and its copy in `out`.
    copied: dict[str, Path] = {}
    for seed in seeds:
        copied[str(seed)] = out / seed.name
        shutil.copyfile(seed.resolve(), out / seed.name)
    kegs = {_keg(str(moltenvk))}
    pending = [binary, *copied.values()]
    while pending:
        for name in _linked(pending.pop()):
            if name not in copied:
                copied[name] = out / Path(name).name
                shutil.copyfile(Path(name).resolve(), copied[name])
                pending.append(copied[name])
                kegs.add(_keg(name))
    for path in [binary, *copied.values()]:
        path.chmod(0o755)
        if path != binary:
            _run("install_name_tool", "-id", f"@rpath/{path.name}", str(path))
        for name in _linked(path):
            _run("install_name_tool", "-change", name, f"@rpath/{Path(name).name}", str(path))
    for name in _run("otool", "-L", str(binary)).splitlines()[1:]:
        old = name.strip().split(" (")[0]
        if old.endswith(core.name) and not old.startswith("@rpath"):
            _run("install_name_tool", "-change", old, f"@rpath/{core.name}", str(binary))
    _run("install_name_tool", "-add_rpath", "@executable_path/../lib", str(binary))
    for path in copied.values():
        _run("install_name_tool", "-add_rpath", "@loader_path", str(path))
    for keg in sorted(k for k in kegs if k is not None):
        kept = licenses / "homebrew" / keg.parent.name
        kept.mkdir(parents=True, exist_ok=True)
        for file in package.license_files(keg):
            shutil.copyfile(file, kept / file.name)


def _sign(bundle: Path) -> None:
    """Ad-hoc signs every Mach-O file inside out, then the bundle: install_name_tool breaks the
    signature an arm64 binary must carry to load."""
    machos = []
    for path in sorted(bundle.rglob("*"), key=lambda p: -len(p.parts)):
        if path.is_file() and not path.is_symlink():
            with path.open("rb") as file:
                if file.read(4) in (b"\xcf\xfa\xed\xfe", b"\xca\xfe\xba\xbe"):
                    machos.append(str(path))
    for chunk in range(0, len(machos), 200):
        _run("codesign", "--force", "--sign", "-", *machos[chunk:chunk + 200])
    _run("codesign", "--force", "--sign", "-", str(bundle))


def folder(cfg: Config) -> Path:
    return cfg.repo / "build" / "package" / "z3d.app"


def build(cfg: Config, bundle: Path) -> Path:
    """Builds `bundle` (z3d.app) from this checkout, replacing an earlier one, and the zip beside it."""
    resources = shortcut.mac_bundle(bundle, cfg.repo / "app" / "icon" / "z3d.svg", LAUNCHER)
    (resources / "bin").mkdir()
    binary = resources / "bin" / "z3d"
    shutil.copy2(app.current_binary(cfg, app.HOST), binary)
    core = next(app.core_prefix(cfg, app.HOST).glob("lib*/libazahar_core*.dylib"))
    _bundle_libraries(binary, core, resources / "lib", resources / "licenses")
    interpreter = package.contents(cfg, resources)
    package.finish(cfg, resources, interpreter)
    _sign(bundle)
    archive = bundle.with_name(ZIP)
    archive.unlink(missing_ok=True)
    _run("ditto", "-c", "-k", "--sequesterRsrc", "--keepParent", str(bundle), str(archive))
    return archive
