"""port-assets' pad glyphs as images: each SVG drawn once, large, by ImageMagick. A checkout
draws one when it first needs it; `z3dmods package` draws the whole set into the package, so a
setup from a package needs no ImageMagick.
"""

from __future__ import annotations

import shutil
import subprocess
from pathlib import Path

from PIL import Image

from z3dmods import config

# The side of a drawn glyph, well above the largest a menu texture takes.
SIDE = 1024
FOLDER = "glyphs"


def _draw(svg: Path, png: Path) -> None:
    """librsvg draws it: rsvg-convert itself where it is installed, else ImageMagick, whose SVG
    reader is librsvg on Fedora. Homebrew's ImageMagick has no librsvg, and its own reader paints
    a gradient fill black (the key cap)."""
    png.parent.mkdir(parents=True, exist_ok=True)
    rsvg = shutil.which("rsvg-convert")
    if rsvg is not None:
        subprocess.run([rsvg, "--width", str(SIDE), "--height", str(SIDE), "--keep-aspect-ratio",
                        "--output", str(png), str(svg)], check=True)
        return
    magick = shutil.which("magick")
    if magick is None:
        raise RuntimeError("rsvg-convert and magick are missing: brew install librsvg (macOS), "
                           "sudo dnf install ImageMagick (Fedora), sudo apt install imagemagick (Debian/Ubuntu)")
    if "RSVG" not in subprocess.run([magick, "-list", "format"], capture_output=True, text=True, check=True).stdout:
        raise RuntimeError("this ImageMagick draws SVG without librsvg, which gets gradients wrong: "
                           "install rsvg-convert (brew install librsvg)")
    subprocess.run([magick, "-background", "none", "-density", "1200", str(svg), "-resize", f"{SIDE}x{SIDE}",
                    str(png)], check=True)


def _name(svg: Path) -> Path:
    return Path(svg.parent.name) / f"{svg.stem}.png"


def image(svg: Path) -> Image.Image:
    """The glyph `svg` of a port-assets set, SIDE pixels square: the package's drawing where
    there is one, else drawn here."""
    if not svg.exists():
        raise ValueError(f"port-assets has no glyph {svg.stem}")
    drawn = config.REPO / config.PREBUILT / FOLDER / _name(svg)
    if not drawn.exists():
        drawn = config.REPO / "build" / FOLDER / _name(svg)
        if not drawn.exists() or drawn.stat().st_mtime < svg.stat().st_mtime:
            _draw(svg, drawn)
    return Image.open(drawn).convert("RGBA")


def save(glyph_set: Path, folder: Path) -> None:
    """Draws every glyph of `glyph_set` for a package, under `folder`."""
    for svg in sorted(glyph_set.glob("*.svg")):
        _draw(svg, folder / _name(svg))
