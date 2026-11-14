"""A game's text font drawn again from an outline font: each glyph of the game's bitmap font
rasterized into its cell at the game's size and at a texture pack's scale.

The outline font is fitted to the bitmap font it replaces: its capitals as tall and on the
same baseline, and tracked until a few lines of prose are as long. The game places glyphs
on whole pixels, so each glyph is stretched across to a whole advance; lines keep an even
rhythm at any scale."""

from __future__ import annotations

import struct
from dataclasses import dataclass
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

# The letters the fit is measured on.
CAPITAL = "H"
RUNNING_TEXT = ("The quick brown fox jumps over the lazy dog. You got it! Press again to draw, and release "
                "to shoot. What's that? I can't believe you're here, 100 Rupees or not...")
# Room beside the advance box for ink that hangs over it, in the game's pixels.
OVERHANG = 3


@dataclass(frozen=True)
class Cell:
    """A glyph of the bitmap font: its cell and where its advance box sits in it."""
    left: int
    advance: int
    image: Image.Image


@dataclass(frozen=True)
class Drawn:
    left: int
    advance: int
    # The cell at the game's size and `scale` times larger, "L" images.
    small: Image.Image
    large: Image.Image


def _cmap(path: Path) -> set[int]:
    """The characters an sfnt font has glyphs for (its cmap's format 4 and 12 subtables)."""
    data = path.read_bytes()
    tables = {data[12 + i * 16: 16 + i * 16]: struct.unpack_from(">II", data, 20 + i * 16)
              for i in range(struct.unpack_from(">H", data, 4)[0])}
    cmap = tables[b"cmap"][0]
    codes: set[int] = set()
    for i in range(struct.unpack_from(">H", data, cmap + 2)[0]):
        table = cmap + struct.unpack_from(">I", data, cmap + 8 + i * 8)[0]
        kind, = struct.unpack_from(">H", data, table)
        if kind == 4:
            count = struct.unpack_from(">H", data, table + 6)[0] // 2
            ends = struct.unpack_from(f">{count}H", data, table + 14)
            starts = struct.unpack_from(f">{count}H", data, table + 16 + count * 2)
            for start, end in zip(starts, ends):
                codes.update(range(start, end + 1))
        elif kind == 12:
            for g in range(struct.unpack_from(">I", data, table + 12)[0]):
                start, end, _ = struct.unpack_from(">III", data, table + 16 + g * 12)
                codes.update(range(start, end + 1))
    codes.discard(0xFFFF)
    return codes


def _rows(image: Image.Image) -> tuple[int, int]:
    """The first inked row and the one after the last."""
    box = image.point(lambda v: 255 if v >= 128 else 0).getbbox()
    if box is None:
        raise ValueError("the fit's capital has no ink")
    return box[1], box[3]


class Outline:
    """An outline font fitted to a bitmap font's cells."""

    def __init__(self, path: Path, weight: int | None, cells: dict[int, Cell], scale: int) -> None:
        self._path, self._weight, self._scale = path, weight, scale
        self._has = _cmap(path)
        capital = cells[ord(CAPITAL)]
        self._width, self._height = capital.image.size
        top, self._baseline = _rows(capital.image)
        trial = self._face(1000)
        ink = trial.getbbox(CAPITAL, anchor="ls")
        self._em = 1000 * (self._baseline - top) / -ink[1]
        self._font = self._face(self._em * scale)
        natural = sum(self._font.getlength(c) for c in RUNNING_TEXT) / scale
        self._tracking = (sum(cells[ord(c)].advance for c in RUNNING_TEXT) - natural) / len(RUNNING_TEXT)

    def _face(self, size: float) -> ImageFont.FreeTypeFont:
        face = ImageFont.truetype(str(self._path), size)
        if self._weight is not None:
            face.set_variation_by_axes([self._weight])
        return face

    def fit(self) -> str:
        return f"{self._em:.2f} px an em, baseline row {self._baseline}, tracking {self._tracking:+.2f} px"

    def draw(self, code: int) -> Drawn | None:
        """A character's glyph in a cell; None where the outline font lacks it."""
        if code not in self._has:
            return None
        scale = self._scale
        boxed = self._font.getlength(chr(code)) / scale + self._tracking
        advance = max(1, round(boxed))
        left = max(0, (self._width - advance) // 2)
        # The advance box with room on both sides, drawn at its own width and then stretched.
        strip = Image.new("L", (round((boxed + 2 * OVERHANG) * scale), self._height * scale))
        ImageDraw.Draw(strip).text(((OVERHANG + self._tracking / 2) * scale, self._baseline * scale), chr(code),
                                   font=self._font, fill=255, anchor="ls")
        strip = strip.resize((round((advance + 2 * OVERHANG * advance / boxed) * scale), strip.height),
                             Image.Resampling.LANCZOS)
        large = Image.new("L", (self._width * scale, self._height * scale))
        large.paste(strip, (round((left - OVERHANG * advance / boxed) * scale), 0))
        small = large.reduce(scale).point(lambda v: (v + 8) // 17 * 17)
        return Drawn(left, advance, small, large)


def enlarged(cell: Image.Image, scale: int) -> Image.Image:
    """A bitmap glyph the outline font lacks, at the pack's scale."""
    return cell.resize((cell.width * scale, cell.height * scale), Image.Resampling.BICUBIC)
