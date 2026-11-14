"""QBF1, OoT3D's text font (decomp/oot3d docs/message.md, "The font files"): a sorted glyph table
and cells of 4-bit alpha, row by row with the high nibble first. Also the texture the game
keeps a font in once our patch makes every text renderer use its static glyph texture
("Drawing glyphs" there; patch/oot3d/font_atlas.cpp fills the same cells)."""

from __future__ import annotations

import struct
from dataclasses import dataclass, replace

from PIL import Image

MAGIC = b"QBF1"
HEADER = struct.Struct("<4sHHHHBBBB")
GLYPH = struct.Struct("<HHBBH")
# The static glyph texture: characters 0 to 255 by code, sixteen a row; the control codes' cells
# hold the characters past 255 in the table's order.
COLUMNS = 16
CHARS = 256
SPARE_CELLS = 0x20


@dataclass(frozen=True)
class Glyph:
    code: int
    cell: int
    # Where the advance box starts in the cell, and its width.
    left: int
    advance: int
    rest: int


@dataclass(frozen=True)
class Font:
    default_char: int
    unknown: int
    cell_width: int
    cell_height: int
    format: int
    glyphs: tuple[Glyph, ...]
    # Each cell as an "L" image of sixteen levels (0, 17, ... 255).
    cells: tuple[Image.Image, ...]

    @classmethod
    def parse(cls, data: bytes) -> Font:
        magic, glyph_count, cell_count, default_char, unknown, bits, width, height, format_ = HEADER.unpack_from(data)
        if magic != MAGIC or bits != 4:
            raise ValueError("not a 4-bit QBF1 font")
        glyphs = tuple(Glyph(*GLYPH.unpack_from(data, HEADER.size + i * GLYPH.size)) for i in range(glyph_count))
        base = HEADER.size + glyph_count * GLYPH.size
        size = width * height // 2
        if len(data) != base + cell_count * size:
            raise ValueError("QBF1 font: the cells do not end at the file's end")
        cells = []
        for i in range(cell_count):
            raw = data[base + i * size: base + (i + 1) * size]
            cell = Image.new("L", (width, height))
            cell.putdata([((raw[p // 2] >> (0 if p % 2 else 4)) & 15) * 17 for p in range(width * height)])
            cells.append(cell)
        return cls(default_char, unknown, width, height, format_, glyphs, tuple(cells))

    def build(self) -> bytes:
        out = bytearray(HEADER.pack(MAGIC, len(self.glyphs), len(self.cells), self.default_char, self.unknown, 4,
                                    self.cell_width, self.cell_height, self.format))
        for glyph in self.glyphs:
            out += GLYPH.pack(glyph.code, glyph.cell, glyph.left, glyph.advance, glyph.rest)
        for cell in self.cells:
            levels = [(v + 8) // 17 for v in cell.tobytes()]
            out += bytes((levels[p] << 4) | levels[p + 1] for p in range(0, len(levels), 2))
        return bytes(out)

    def redrawn(self, drawn: dict[int, tuple[int, int, Image.Image]]) -> Font:
        """The font with the glyphs of `drawn` (code: left, advance, cell) in place of its own."""
        cells = list(self.cells)
        glyphs = []
        for glyph in self.glyphs:
            if glyph.code in drawn:
                left, advance, cell = drawn[glyph.code]
                cells[glyph.cell] = cell
                glyph = replace(glyph, left=left, advance=advance)
            glyphs.append(glyph)
        return replace(self, glyphs=tuple(glyphs), cells=tuple(cells))

    def texture_cells(self) -> list[int | None]:
        """The font's cell in each cell of the static glyph texture; None where the lookup fails
        twice and the texture's cell stays empty."""
        by_code = {glyph.code: glyph.cell for glyph in self.glyphs}
        spare = [glyph.code for glyph in self.glyphs if glyph.code >= CHARS][:SPARE_CELLS]
        out: list[int | None] = []
        for index in range(CHARS):
            code = index if index >= SPARE_CELLS else (spare[index] if index < len(spare) else 0)
            cell = by_code.get(code, by_code.get(self.default_char))
            out.append(cell if cell is not None and cell < len(self.cells) else None)
        return out


def static_texture(font: Font, cells: tuple[Image.Image, ...] | None = None) -> Image.Image:
    """The static glyph texture's alpha, top row first; `cells` stand in for the font's own (a
    larger copy of each)."""
    cells = cells or font.cells
    scale = cells[0].width // font.cell_width
    pitch_x, pitch_y = _pow2(font.cell_width) * scale, _pow2(font.cell_height) * scale
    texture = Image.new("L", (pitch_x * COLUMNS, pitch_y * (CHARS // COLUMNS)))
    for index, cell in enumerate(font.texture_cells()):
        if cell is not None:
            texture.paste(cells[cell], ((index % COLUMNS) * pitch_x, (index // COLUMNS) * pitch_y))
    return texture


def _pow2(value: int) -> int:
    return 1 << (value - 1).bit_length()
