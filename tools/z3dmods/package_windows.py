"""The z3d package for Windows (x86-64), built in MSYS2's clang64 environment: the package folder
package.py describes, and the zip of it a release carries.

    z3d.exe             the launcher (app/launcher): python\\pythonw.exe -I -m z3dmods.packed play
    bin/z3d.exe, *.dll  the app, the core and every clang64 library they link; Windows loads a
                        program's libraries from its own folder first
    python/             a relocatable CPython (uv's), with the locked wheels in Lib/site-packages

Vulkan is the system's vulkan-1.dll, which every Vulkan driver installs.
"""

from __future__ import annotations

import shutil
import subprocess
from pathlib import Path

from z3dmods import app, package
from z3dmods.config import Config

ZIP = "z3d-windows-x86_64.zip"
# Where MSYS2's clang64 environment keeps its programs and libraries, as MSYS2's tools name it.
CLANG64 = "/clang64/"


def _run(*command: str) -> str:
    return subprocess.run(command, check=True, capture_output=True, text=True).stdout


def _windows_path(msys: str) -> Path:
    return Path(_run("cygpath", "-w", msys).strip())


def _libraries(binary: Path, core: Path, licenses: Path) -> None:
    """Copies `core` and every clang64 library `binary` needs beside it, and the licence folder of
    each MSYS2 package they come from into `licenses`/msys2/<package>."""
    shutil.copyfile(core, binary.parent / core.name)
    linked = []
    for line in _run("ldd", str(binary)).splitlines():
        _name, _, rest = line.strip().partition(" => ")
        if rest.startswith(CLANG64):
            linked.append(rest.split()[0])
    for library in linked:
        shutil.copyfile(_windows_path(library), binary.parent / Path(library).name)
    owners = {line.split()[-2] for line in _run("pacman", "-Qo", *linked).splitlines()}
    for owner in sorted(owners):
        kept = _windows_path(f"{CLANG64}share/licenses/{owner.removeprefix('mingw-w64-clang-x86_64-')}")
        if kept.is_dir():
            shutil.copytree(kept, licenses / "msys2" / owner)


def folder(cfg: Config) -> Path:
    return cfg.repo / "build" / "package" / "z3d"


def build(cfg: Config, out: Path) -> Path:
    """Builds the package folder `out` from this checkout, replacing an earlier one, and the zip beside it."""
    if out.exists():
        shutil.rmtree(out)
    (out / "bin").mkdir(parents=True)
    built = app.current_binary(cfg, app.HOST)
    binary = out / "bin" / built.name
    shutil.copy2(built, binary)
    shutil.copy2(built.with_name("z3d_launcher.exe"), out / "z3d.exe")
    core = next(app.core_prefix(cfg, app.HOST).glob("**/libazahar_core*.dll"))
    _libraries(binary, core, out / "licenses")
    interpreter = package.contents(cfg, out)
    package.finish(cfg, out, interpreter)
    archive = out.with_name(ZIP)
    archive.unlink(missing_ok=True)
    shutil.make_archive(str(archive.with_suffix("")), "zip", out.parent, out.name)
    return archive
