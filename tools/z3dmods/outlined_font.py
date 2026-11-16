"""An outlined bitmap font of the game drawn again at a texture pack's scale: white glyphs in a
dark edge with a drop shadow (OoT3D's message/sys8.qbf, the date and time on file select).

The font's file stays as it is, so the game lays the text out as before; only its static glyph
texture (qbf.static_texture) gets a larger image. Each glyph's white fill is drawn from an outline
font stretched to the fill's box in the game's cell, and the edge and the shadow are made from it
as the game's are: the edge one pixel out, the shadow the edged glyph one pixel lower. Their
shades are the game's own, read from its cells."""

from __future__ import annotations

import io
import statistics
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFont

from z3dmods import pica_texture, qbf

# A pixel of the game's cells is the glyph's fill from this intensity up, at full alpha.
FILL = 0xAA
# The size the outline font is first drawn at, before it is fitted to a fill.
TRIAL = 512


def _shades(font: qbf.Font) -> tuple[int, int, int]:
    """The edge's intensity, the shadow's intensity and its alpha, as most of the game's pixels have them."""
    edge, shadow, shadow_alpha = [], [], []
    for cell in font.cells:
        for intensity, alpha in zip(cell.getchannel("L").tobytes(), cell.getchannel("A").tobytes()):
            if alpha == 255 and intensity < 0x66:
                edge.append(intensity)
            elif 0 < alpha < 255:
                shadow.append(intensity)
                shadow_alpha.append(alpha)
    return statistics.median_low(edge), statistics.median_low(shadow), statistics.median_low(shadow_alpha)


def _fill_box(cell: Image.Image) -> tuple[int, int, int, int] | None:
    fill = ImageChops.multiply(cell.getchannel("L").point(lambda v: 255 if v >= FILL else 0),
                               cell.getchannel("A").point(lambda v: 255 if v == 255 else 0))
    return fill.getbbox()


def _dilated(mask: Image.Image, radius: int) -> Image.Image:
    """`mask` grown by a disc of `radius` pixels."""
    out = mask
    for dy in range(-radius, radius + 1):
        for dx in range(-radius, radius + 1):
            if (dx or dy) and dx * dx + dy * dy <= radius * radius:
                out = ImageChops.lighter(out, ImageChops.offset(mask, dx, dy))
    return out


def _layer(intensity: int, alpha: Image.Image) -> Image.Image:
    grey = Image.new("L", alpha.size, intensity)
    return Image.merge("RGBA", (grey, grey, grey, alpha))


class Redrawn:
    def __init__(self, font: qbf.Font, outline: Path, weight: int | None, scale: int) -> None:
        self._font, self._scale = font, scale
        self._face = ImageFont.truetype(str(outline), TRIAL)
        if weight is not None:
            self._face.set_variation_by_axes([weight])
        self._edge, self._shadow, self._shadow_alpha = _shades(font)

    def _cell(self, code: int, box: tuple[int, int, int, int]) -> Image.Image:
        """A glyph's cell at the pack's scale, as RGBA: its fill in `box`, edged and shadowed."""
        scale, font = self._scale, self._font
        trial = Image.new("L", (TRIAL * 2, TRIAL * 2))
        ImageDraw.Draw(trial).text((TRIAL // 2, TRIAL // 2), chr(code), font=self._face, fill=255)
        ink = trial.crop(trial.getbbox())
        left, top, right, bottom = (edge * scale for edge in box)
        # Room for the edge and the shadow below it, so neither wraps around the cell.
        pad = 2 * scale
        fill = Image.new("L", (font.cell_width * scale + 2 * pad, font.cell_height * scale + 2 * pad))
        fill.paste(ink.resize((right - left, bottom - top), Image.Resampling.LANCZOS), (left + pad, top + pad))
        edged = _dilated(fill, scale)
        shadow = ImageChops.offset(edged, 0, scale).point(lambda v: v * self._shadow_alpha // 255)
        cell = _layer(self._shadow, shadow)
        cell.alpha_composite(_layer(self._edge, edged))
        cell.alpha_composite(_layer(255, fill))
        return cell.crop((pad, pad, pad + font.cell_width * scale, pad + font.cell_height * scale))

    def textures(self, new_hash: bool) -> dict[str, bytes]:
        """The pack's image of the font's static glyph texture, by its file name."""
        font, scale = self._font, self._scale
        blank = Image.new("LA", (font.cell_width * scale, font.cell_height * scale))
        code_of = {}
        for glyph in font.glyphs:
            code_of.setdefault(glyph.cell, glyph.code)
        large = []
        for index, cell in enumerate(font.cells):
            box = _fill_box(cell)
            if box is None or index not in code_of:
                large.append(blank)
                continue
            rgba = self._cell(code_of[index], box)
            large.append(Image.merge("LA", (rgba.getchannel("R"), rgba.getchannel("A"))))
        game = qbf.static_texture(font)
        intensity, alpha = game.split()
        decoded = Image.merge("RGBA", (intensity, intensity, intensity, alpha))
        name = pica_texture.pack_name(decoded, pica_texture.encode_ia4(game), pica_texture.IA4, new_hash)
        big = qbf.static_texture(font, tuple(large)).convert("RGBA")
        buffer = io.BytesIO()
        big.save(buffer, "PNG")
        return {name: buffer.getvalue()}
