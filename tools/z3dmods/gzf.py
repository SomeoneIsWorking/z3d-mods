"""GZFX, MM3D's text font (decomp/mm3d docs/formats/gzf.md): a sorted glyph table and sheets of
4-bit alpha that the game hands to the GPU whole, so a texture pack can replace them."""

from __future__ import annotations

import struct
from dataclasses import dataclass, replace

from PIL import Image

from z3dmods import pica_texture

MAGIC = b"GZFX"
SHEET = struct.Struct("<IHH")
GLYPH = struct.Struct("<IHHHBB")
SHEET_TABLE = 0x08
SHEET_COUNT = 0x10
GLYPH_COUNT = 0x14
CELL_WIDTH = 0x22
CELL_HEIGHT = 0x28


@dataclass(frozen=True)
class Glyph:
    code: int
    advance: int
    sheet: int
    # Where the advance box starts in the cell.
    left: int
    column: int
    row: int


@dataclass(frozen=True)
class Font:
    # The file up to the first sheet, whose tables build() writes again.
    head: bytes
    cell_width: int
    cell_height: int
    glyphs: tuple[Glyph, ...]
    # Each sheet's alpha as an "L" image of sixteen levels.
    sheets: tuple[Image.Image, ...]

    @classmethod
    def parse(cls, data: bytes) -> Font:
        if data[:4] != MAGIC:
            raise ValueError("not a GZFX font")
        table, = struct.unpack_from("<H", data, SHEET_TABLE)
        sheet_count, glyph_count = struct.unpack_from("<II", data, SHEET_COUNT)
        sheets = []
        first = len(data)
        for i in range(sheet_count):
            offset, width, height = SHEET.unpack_from(data, table + i * SHEET.size)
            first = min(first, offset)
            rgba = pica_texture.decode(data[offset: offset + width * height // 2], width, height, pica_texture.A4)
            sheets.append(rgba.getchannel("A"))
        glyphs = tuple(Glyph(*GLYPH.unpack_from(data, table + sheet_count * SHEET.size + i * GLYPH.size))
                       for i in range(glyph_count))
        return cls(data[:first], struct.unpack_from("<H", data, CELL_WIDTH)[0],
                   struct.unpack_from("<I", data, CELL_HEIGHT)[0], glyphs, tuple(sheets))

    def sheet_bytes(self) -> list[bytes]:
        """Each sheet as the game holds it: what Azahar's hash of a texture's bytes reads."""
        return [pica_texture.encode_a4(sheet) for sheet in self.sheets]

    def build(self) -> bytes:
        out = bytearray(self.head)
        table, = struct.unpack_from("<H", out, SHEET_TABLE)
        for i, glyph in enumerate(self.glyphs):
            GLYPH.pack_into(out, table + len(self.sheets) * SHEET.size + i * GLYPH.size, glyph.code, glyph.advance,
                            glyph.sheet, glyph.left, glyph.column, glyph.row)
        for i, raw in enumerate(self.sheet_bytes()):
            offset, _, _ = SHEET.unpack_from(out, table + i * SHEET.size)
            if offset != len(out):
                raise ValueError("GZFX font: the sheets do not follow one another")
            out += raw
        return bytes(out)

    def box(self, glyph: Glyph, scale: int = 1) -> tuple[int, int, int, int]:
        """A glyph's cell in its sheet, or in a copy `scale` times larger."""
        x, y = glyph.column * self.cell_width * scale, glyph.row * self.cell_height * scale
        return x, y, x + self.cell_width * scale, y + self.cell_height * scale

    def cell(self, glyph: Glyph) -> Image.Image:
        return self.sheets[glyph.sheet].crop(self.box(glyph))

    def redrawn(self, drawn: dict[int, tuple[int, int, Image.Image]]) -> Font:
        """The font with the glyphs of `drawn` (code: left, advance, cell) in place of its own."""
        sheets = [sheet.copy() for sheet in self.sheets]
        glyphs = []
        for glyph in self.glyphs:
            if glyph.code in drawn:
                left, advance, cell = drawn[glyph.code]
                sheets[glyph.sheet].paste(cell, self.box(glyph)[:2])
                glyph = replace(glyph, left=left, advance=advance)
            glyphs.append(glyph)
        return replace(self, glyphs=tuple(glyphs), sheets=tuple(sheets))
