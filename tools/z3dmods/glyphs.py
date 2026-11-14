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
    magick = shutil.which("magick")
    if magick is None:
        raise RuntimeError("magick is missing: sudo dnf install ImageMagick (Fedora), "
                           "sudo apt install imagemagick (Debian/Ubuntu)")
    png.parent.mkdir(parents=True, exist_ok=True)
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
