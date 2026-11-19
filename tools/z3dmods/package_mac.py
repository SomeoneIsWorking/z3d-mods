"""The z3d package for macOS (Apple silicon): `z3d.app`, whose Contents/Resources is the package
folder package.py describes, and the zip of it a release carries.

    Contents/MacOS/z3d          the launcher: Resources/python/bin/python3 -I -m z3dmods.packed play
    Contents/Resources/bin/z3d  the app, built on this Mac against Homebrew's libraries
    Contents/Resources/lib/     the core, the Vulkan loader, MoltenVK and every Homebrew library they
                                and the app link, each named @rpath/<file> so the bundle needs no Homebrew
    Contents/Resources/vulkan/  MoltenVK's driver file, which the launcher hands the loader

The core and SDL open Vulkan as libvulkan.dylib, which dyld finds in lib/ on the library fallback
path config.app_env sets. Everything is signed ad hoc.
"""

from __future__ import annotations

import re
import shutil
import subprocess
from pathlib import Path

from z3dmods import app, package, shortcut, sources
from z3dmods.config import Config

ZIP = "z3d-macos-arm64.zip"
# Where Homebrew installs on Apple silicon; a library linked from anywhere else is the system's.
HOMEBREW = "/opt/homebrew"
LAUNCHER = """#!/bin/sh
# z3d: opens the title picker, which asks for your folders on its first run.
here=$(cd "$(dirname "$0")/../Resources" && pwd)
# The bundled Vulkan loader finds the bundled MoltenVK through this driver file.
export VK_DRIVER_FILES="$here/vulkan/icd.d/MoltenVK_icd.json"
exec "$here/python/bin/python3" -I -m z3dmods.packed play "$@"
"""
# MoltenVK's driver file for the loader, relative to Resources/vulkan/icd.d.
DRIVER = """{
    "file_format_version": "1.0.0",
    "ICD": {
        "library_path": "../../lib/libMoltenVK.dylib",
        "api_version": "1.4.0",
        "is_portability_driver": true
    }
}
"""


def _flags(cfg: Config) -> list[str]:
    """Keeps this Mac's folders out of the package, as the Linux container build does: a source
    file is named from its checkout, and the app has no run path of its own (the bundle adds
    @executable_path/../lib)."""
    home = Path.home()
    named = {app.azahar_source(cfg): "azahar", cfg.repo: "z3d", cfg.dependencies / "azahar-core": "azahar-core",
             **{home / checkout: name for name, checkout in sources.HOME_CHECKOUTS.items()}, home: "builder"}
    maps = " ".join(f"-ffile-prefix-map={folder}={name}" for folder, name in named.items())
    return [f"-DCMAKE_C_FLAGS={maps}", f"-DCMAKE_CXX_FLAGS={maps}", "-DCMAKE_SKIP_RPATH=ON"]


# The package's own core and app, built with _flags apart from the development build (app.HOST).
BUILDER = app.Builder(f"{app.TOOLCHAIN}-package", lambda cfg: cfg.repo / "build" / "package-app",
                      lambda _cfg, command: subprocess.run(command, check=True), _flags)


def _run(*command: str) -> str:
    return subprocess.run(command, check=True, capture_output=True, text=True).stdout


def _linked(path: Path, origin: Path | None = None) -> list[tuple[str, Path]]:
    """The Homebrew libraries the Mach-O file `path` names, as it names them and where each is.
    A Homebrew library may name a sibling @rpath/<file> or @loader_path/<file> (libjxl names
    libjxl_cms so); that is the file beside `origin`, the Homebrew file `path` was copied from."""
    lines = _run("otool", "-L", str(path)).splitlines()[1:]
    names = [line.strip().split(" (")[0] for line in lines]
    found = []
    for name in names:
        if name.startswith(HOMEBREW):
            found.append((name, Path(name)))
        elif origin is not None and name.startswith(("@rpath/", "@loader_path/")):
            beside = origin.parent / name.split("/", 1)[1]
            if beside.exists() and beside.resolve() != origin.resolve():
                found.append((name, beside))
    return found


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
    # The loader, not MoltenVK alone: the picker and the core ask for VK_KHR_portability_enumeration,
    # which only the loader has. The core and SDL open it as libvulkan.dylib.
    loader = Path(HOMEBREW) / "opt" / "vulkan-loader" / "lib" / "libvulkan.1.dylib"
    seeds = [core, moltenvk, loader]
    # Each library as the files that link it name it, and its copy in `out`.
    copied: dict[str, Path] = {}
    # Each copy in `out` and the file it was copied from.
    origins: dict[Path, Path | None] = {binary: None}
    for seed in seeds:
        copied[str(seed.resolve())] = out / seed.name
        shutil.copyfile(seed.resolve(), out / seed.name)
        origins[out / seed.name] = seed.resolve()
    kegs = {_keg(str(moltenvk)), _keg(str(loader))}
    pending = [binary, *copied.values()]
    while pending:
        path = pending.pop()
        for name, source in _linked(path, origins[path]):
            key = str(source.resolve())
            if key not in copied:
                copied[key] = out / source.name
                shutil.copyfile(source.resolve(), copied[key])
                origins[copied[key]] = source.resolve()
                pending.append(copied[key])
                kegs.add(_keg(str(source)))
    for path in [binary, *copied.values()]:
        path.chmod(0o755)
        if path != binary:
            _run("install_name_tool", "-id", f"@rpath/{path.name}", str(path))
        for name, source in _linked(path, origins[path]):
            if name != f"@rpath/{source.name}":
                _run("install_name_tool", "-change", name, f"@rpath/{source.name}", str(path))
    for name in _run("otool", "-L", str(binary)).splitlines()[1:]:
        old = name.strip().split(" (")[0]
        if old.endswith(core.name) and not old.startswith("@rpath"):
            _run("install_name_tool", "-change", old, f"@rpath/{core.name}", str(binary))
    (out / "libvulkan.dylib").symlink_to(loader.name)
    drivers = out.parent / "vulkan" / "icd.d"
    drivers.mkdir(parents=True)
    (drivers / "MoltenVK_icd.json").write_text(DRIVER)
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
    shutil.copy2(app.current_binary(cfg, BUILDER), binary)
    core = next(app.core_prefix(cfg, BUILDER).glob("lib*/libazahar_core*.dylib"))
    _bundle_libraries(binary, core, resources / "lib", resources / "licenses")
    interpreter = package.contents(cfg, resources)
    # uv names libpython by the folder it unpacked it into, here inside this bundle; the
    # interpreter does not link it, and an extension that does finds it through @rpath.
    for library in (resources / "python" / "lib").glob("libpython*.dylib"):
        if not library.is_symlink():
            _run("install_name_tool", "-id", f"@rpath/{library.name}", str(library))
    package.finish(cfg, resources, interpreter)
    _sign(bundle)
    archive = bundle.with_name(ZIP)
    archive.unlink(missing_ok=True)
    _run("ditto", "-c", "-k", "--sequesterRsrc", "--keepParent", str(bundle), str(archive))
    return archive
