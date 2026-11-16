"""The desktop shortcut for this machine (never committed): the z3d app's title picker, launched
through `z3dmods play` on the newest build (app.binary builds only when none exists)."""

from __future__ import annotations

import shlex
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from z3dmods import play
from z3dmods.config import Config

# Per-game entries from before the picker.
RETIRED = ("z3d-oot3d.desktop", "z3d-mm3d.desktop")


def folders() -> list[Path]:
    return [Path.home() / ".local/share/applications", Path.home() / "Desktop"]


def exec_line(cfg: Config) -> str:
    return " ".join(f'"{part}"' for part in (*cfg.tool_command, "play"))


def desktop_entry(exec_: str, folder: Path | None = None) -> str:
    """The entry that opens the title picker; `folder` is its working directory."""
    return ("[Desktop Entry]\nType=Application\nName=z3d\n"
            "Comment=Ocarina of Time 3D and Majora's Mask 3D with their mods\n"
            f"Exec={exec_}\n" + (f"Path={folder}\n" if folder else "") +
            "Icon=z3d\nTerminal=false\nCategories=Game;\n")


# macOS: the picker as an app in the player's Applications folder.
MAC_APP = Path.home() / "Applications" / "z3d.app"
MAC_BUNDLE_ID = "io.github.someoneisworking.z3d"


def _mac_icon(svg: Path, icns: Path) -> None:
    """The app icon from the SVG: Quick Look draws it, sips scales each size, iconutil packs them."""
    with tempfile.TemporaryDirectory() as scratch:
        drawn = Path(scratch)
        subprocess.run(["qlmanage", "-t", "-s", "1024", "-o", str(drawn), str(svg)], check=True, capture_output=True)
        png = drawn / f"{svg.name}.png"
        iconset = drawn / "z3d.iconset"
        iconset.mkdir()
        for size in (16, 32, 128, 256, 512):
            for scale in (1, 2):
                name = f"icon_{size}x{size}{'@2x' if scale == 2 else ''}.png"
                subprocess.run(["sips", "-z", str(size * scale), str(size * scale), str(png), "--out",
                                str(iconset / name)], check=True, capture_output=True)
        subprocess.run(["iconutil", "-c", "icns", str(iconset), "-o", str(icns)], check=True)


def _mac_install(cfg: Config) -> list[Path]:
    contents = MAC_APP / "Contents"
    if MAC_APP.exists():
        shutil.rmtree(MAC_APP)
    (contents / "MacOS").mkdir(parents=True)
    (contents / "Resources").mkdir()
    _mac_icon(cfg.repo / "app" / "icon" / "z3d.svg", contents / "Resources" / "z3d.icns")
    (contents / "Info.plist").write_text(
        '<?xml version="1.0" encoding="UTF-8"?>\n'
        '<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">\n'
        '<plist version="1.0"><dict>\n'
        "<key>CFBundleName</key><string>z3d</string>\n"
        f"<key>CFBundleIdentifier</key><string>{MAC_BUNDLE_ID}</string>\n"
        "<key>CFBundleExecutable</key><string>z3d</string>\n"
        "<key>CFBundleIconFile</key><string>z3d</string>\n"
        "<key>CFBundlePackageType</key><string>APPL</string>\n"
        "<key>LSApplicationCategoryType</key><string>public.app-category.games</string>\n"
        "<key>NSHighResolutionCapable</key><true/>\n"
        "</dict></plist>\n")
    launcher = contents / "MacOS" / "z3d"
    command = " ".join(shlex.quote(part) for part in (*cfg.tool_command, "play"))
    log = cfg.app_data / "launch.log"
    # Finder's PATH is /usr/bin:/bin:/usr/sbin:/sbin: the tools also look up uv and Homebrew's
    # programs (magick, the Vulkan loader's MoltenVK) by name.
    path = ":".join(dict.fromkeys([str(Path(cfg.tool_command[0]).parent), "/opt/homebrew/bin",
                                   "/usr/bin", "/bin", "/usr/sbin", "/sbin"]))
    launcher.write_text(f"#!/bin/sh\nexport PATH={shlex.quote(path)}\n"
                        f"mkdir -p {shlex.quote(str(log.parent))}\n"
                        f"exec {command} >{shlex.quote(str(log))} 2>&1\n")
    launcher.chmod(0o755)
    return [MAC_APP]


def install(cfg: Config) -> list[Path]:
    if sys.platform == "darwin":
        return _mac_install(cfg)
    icons = Path.home() / ".local/share/icons/hicolor/scalable/apps"
    icons.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(cfg.repo / "app" / "icon" / "z3d.svg", icons / "z3d.svg")
    entry = desktop_entry(exec_line(cfg))
    written = []
    folders()[0].mkdir(parents=True, exist_ok=True)
    for folder in folders():
        if not folder.is_dir():
            continue
        for old in RETIRED:
            (folder / old).unlink(missing_ok=True)
        path = folder / f"{play.DESKTOP_ID}.desktop"
        path.write_text(entry)
        path.chmod(0o755)
        written.append(path)
    return written


def desktop_exec(path: Path) -> str:
    """The Exec line of an installed entry, as the desktop runs it."""
    for line in path.read_text().splitlines():
        if line.startswith("Exec="):
            return line.removeprefix("Exec=")
    raise RuntimeError(f"{path} has no Exec line")
