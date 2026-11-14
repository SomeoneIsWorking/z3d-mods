"""Replacements for a game's menu textures: a base mod's edits at a texture pack's scale, and pad glyphs.

A base mod that edits a vanilla texture changes its Azahar hash, so the texture pack's high-resolution
copy of the vanilla texture no longer applies. Each replacement starts from that copy and redoes the
mod's edits at the pack's scale, as an edit table lists them (sets/hud/*.json): re-lettered areas keep
the pack's vanilla art, moved art is copied, new lettering is drawn in port-assets' key font, and
the 3DS button prompts the mod leaves become port-assets' Xbox pad glyphs. The result is named after
the mod's texture, so the pack covers the mod's version too.

With no base mod behind a texture (MM3D) only the prompts change, and the replacement carries the
name of the pack's own copy, which the install then leaves out. A texture inside a GAR archive is named
`<archive>.gar/<file>.ctxb`.
"""

from __future__ import annotations

import io
import json
import shutil
import subprocess
from dataclasses import dataclass
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

from z3dmods import ctr, ctxb, gar, pica_texture
from z3dmods.texture_pack import TexturePack

# A premultiplied channel difference above this is an edit, not the mod's re-encoding.
EDIT_THRESHOLD = 24
STYLES = {"white": (255, 255, 255), "yellow": (255, 255, 0)}
OUTLINE = (0, 0, 0)
SUPERSAMPLE = 4


@dataclass(frozen=True)
class Texture:
    """A menu texture: the vanilla one, the mod's edit of it, and the pack's copy of the vanilla."""

    vanilla: Image.Image
    modded: Image.Image
    # The mod's texture as the game holds it, which Azahar may name the replacement by.
    modded_data: bytes
    gl_format: int
    hires: Image.Image

    @property
    def scale(self) -> int:
        return self.hires.width // self.vanilla.width


def _premultiplied(image: Image.Image) -> Image.Image:
    alpha = image.getchannel("A")
    channels = [ImageChops.multiply(image.getchannel(c), alpha) for c in "RGB"]
    return Image.merge("RGBA", (*channels, alpha))


def _edited(texture: Texture) -> Image.Image:
    """Where the mod changed the vanilla texture, as a mask."""
    diff = ImageChops.difference(_premultiplied(texture.modded), _premultiplied(texture.vanilla))
    channels = [diff.getchannel(c).point(lambda v: 255 if v > EDIT_THRESHOLD else 0) for c in "RGBA"]
    mask = channels[0]
    for channel in channels[1:]:
        mask = ImageChops.lighter(mask, channel)
    return mask


def _scaled(box: list[float], scale: int) -> tuple[int, int, int, int]:
    return tuple(round(v * scale) for v in box)


def _fill(image: Image.Image, unknown: Image.Image) -> Image.Image:
    """`image` with the `unknown` pixels filled smoothly from the known ones around them."""

    def pull(values: Image.Image) -> Image.Image:
        weighted = values.copy()
        weighted.putalpha(ImageChops.invert(unknown))
        levels = [weighted]
        while min(levels[-1].size) > 1:
            last = levels[-1]
            levels.append(last.convert("RGBa").resize(((last.width + 1) // 2, (last.height + 1) // 2),
                                                      Image.BOX).convert("RGBA"))
        filled = levels[-1]
        for level in reversed(levels[:-1]):
            filled = Image.alpha_composite(filled.resize(level.size, Image.BILINEAR), level)
        return filled

    colour = pull(image.convert("RGB"))
    alpha = pull(Image.merge("RGB", [image.getchannel("A")] * 3)).getchannel("R")
    colour.putalpha(alpha)
    out = image.copy()
    out.paste(colour, mask=unknown)
    return out


def _lettering(size: tuple[int, int], text: str, font: Path, fill: tuple[int, int, int],
               stroke: int) -> Image.Image:
    """`text` as large as fits `size`, centred, with an outline of `stroke` pixels."""
    width, height = (v * SUPERSAMPLE for v in size)
    stroke *= SUPERSAMPLE
    trial = ImageFont.truetype(str(font), 100)
    left, top, right, bottom = trial.getbbox(text, stroke_width=stroke * 100 // max(1, height))
    points = int(100 * min(width / (right - left), height / (bottom - top)))
    face = ImageFont.truetype(str(font), max(1, points))
    left, top, right, bottom = face.getbbox(text, stroke_width=stroke)
    layer = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    ImageDraw.Draw(layer).text(((width - (right - left)) / 2 - left, (height - (bottom - top)) / 2 - top),
                               text, font=face, fill=(*fill, 255), stroke_width=stroke,
                               stroke_fill=(*OUTLINE, 255))
    return layer.resize(size, Image.LANCZOS)


def _letter_mask(region: Image.Image) -> Image.Image:
    """The pixels of the letter painted on a plain face: those far from the face's own tone."""
    gray = region.convert("L")
    face = sorted(gray.getdata())[len(gray.getdata()) * 3 // 4]
    return gray.point(lambda v: 255 if abs(v - face) > 48 else 0)


def _yellow_mask(region: Image.Image) -> Image.Image:
    r, g, b, a = region.split()
    hue, saturation, _ = Image.merge("RGB", (r, g, b)).convert("HSV").split()
    yellow = ImageChops.multiply(hue.point(lambda h: 255 if 25 <= h <= 50 else 0),
                                 saturation.point(lambda s: 255 if s > 90 else 0))
    return ImageChops.multiply(yellow, a.point(lambda v: 255 if v > 32 else 0))


def _grown(mask: Image.Image, pixels: int) -> Image.Image:
    return mask.filter(ImageFilter.MaxFilter(2 * pixels + 1))


def _dpad(size: tuple[int, int], colour: tuple[int, int, int, int], scale: int) -> Image.Image:
    width, height = (v * SUPERSAMPLE for v in size)
    inset = scale * SUPERSAMPLE
    arm = width * 2 // 7
    left, right = (width - arm) // 2, (width + arm) // 2
    top, bottom = (height - arm) // 2, (height + arm) // 2
    cross = [(left, inset), (right, inset), (right, top), (width - inset, top), (width - inset, bottom),
             (right, bottom), (right, height - inset), (left, height - inset), (left, bottom),
             (inset, bottom), (inset, top), (left, top)]
    layer = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    ImageDraw.Draw(layer).polygon(cross, outline=colour, width=scale * SUPERSAMPLE)
    return layer.resize(size, Image.LANCZOS)


# A dimmed prompt (a button that cannot be used now) keeps this share of its brightness.
DIMMED = 0.45


def _glyph(size: tuple[int, int], svg: Path, dimmed: bool) -> Image.Image:
    """A port-assets glyph as large as fits `size`, centred."""
    if not svg.exists():
        raise ValueError(f"port-assets has no glyph {svg.stem}")
    magick = shutil.which("magick")
    if magick is None:
        raise RuntimeError("magick is missing: sudo dnf install ImageMagick (Fedora), "
                           "sudo apt install imagemagick (Debian/Ubuntu)")
    side = min(size) * SUPERSAMPLE
    png = subprocess.run([magick, "-background", "none", "-density", "1200", str(svg), "-resize", f"{side}x{side}",
                          "png:-"], capture_output=True, check=True).stdout
    glyph = Image.open(io.BytesIO(png)).convert("RGBA").resize((min(size), min(size)), Image.LANCZOS)
    if dimmed:
        r, g, b, a = glyph.split()
        glyph = Image.merge("RGBA", (*(c.point(lambda v: int(v * DIMMED)) for c in (r, g, b)), a))
    layer = Image.new("RGBA", size, (0, 0, 0, 0))
    layer.alpha_composite(glyph, ((size[0] - glyph.width) // 2, (size[1] - glyph.height) // 2))
    return layer


# A chord's held button takes this much of the cell's height, above the pressed one.
CHORD_HELD = 0.5


def _chord(size: tuple[int, int], held: Path, pressed: Path) -> Image.Image:
    """Two glyphs in one cell: the held button across the top, the pressed one under it."""
    layer = Image.new("RGBA", size, (0, 0, 0, 0))
    top = round(size[1] * CHORD_HELD)
    # A bumper's pill is half as tall as its cell, so the full-width glyph is cropped to its middle.
    wide = _glyph((size[0], size[0]), held, False)
    layer.alpha_composite(wide.crop((0, (size[0] - top) // 2, size[0], (size[0] - top) // 2 + top)), (0, 0))
    layer.alpha_composite(_glyph((size[0], size[1] - top), pressed, False), (0, top))
    return layer


def _inlay_ground(image: Image.Image, box: tuple[int, int, int, int], scale: int) -> Image.Image:
    """The art under a prompt drawn on a button face: each row of `box` blended between the
    art just left and just right of it."""
    x0, y0, x1, y1 = box
    height = y1 - y0
    edges = Image.new("RGBa", (2, height))
    edges.paste(image.crop((max(0, x0 - scale), y0, x0, y1)).convert("RGBa").resize((1, height), Image.BOX), (0, 0))
    edges.paste(image.crop((x1, y0, min(image.width, x1 + scale), y1)).convert("RGBa").resize((1, height), Image.BOX),
                (1, 0))
    return edges.resize((x1 - x0, height), Image.BILINEAR).convert("RGBA")


def _container(path: str) -> str:
    """The game file a texture path is in: itself, or its GAR archive."""
    archive, inside, _ = path.partition(".gar/")
    return archive + ".gar" if inside else path


def _bundle(read, path: str) -> bytes:
    """A CTXB file, loose or inside a GAR archive."""
    container = _container(path)
    return read(path) if container == path else gar.files(read(container))[path[len(container) + 1:]]


def _dominant(image: Image.Image) -> tuple[int, int, int, int]:
    opaque = [p for p in image.getdata() if p[3] > 128]
    return tuple(sorted(opaque)[len(opaque) // 2]) if opaque else (255, 255, 255, 255)


def _darkest(image: Image.Image, mask: Image.Image) -> tuple[int, int, int]:
    """The letter's ink: its darkest opaque pixel."""
    pixels = [p for p, m in zip(image.getdata(), mask.getdata()) if m and p[3] > 128]
    return min(pixels, key=lambda p: p[0] + p[1] + p[2])[:3] if pixels else OUTLINE


def compose(texture: Texture, edits: list[dict], others: dict[str, Texture], font: Path,
            glyphs: Path) -> Image.Image:
    """The pack's vanilla copy with the mod's edits redone at its scale."""
    scale = texture.scale
    out = texture.hires.copy()
    covered = Image.new("L", texture.vanilla.size, 0)
    draw = ImageDraw.Draw(covered)
    clear = (0, 0, 0, 0)
    for edit in edits:
        kind = next(k for k in ("keep", "copy", "clear", "label", "relabel", "shell", "dpad", "glyph", "chord")
                    if k in edit)
        box = edit["box"] if "box" in edit else edit[kind]
        draw.rectangle((box[0], box[1], box[2] - 1, box[3] - 1), fill=255)
        x0, y0, x1, y1 = _scaled(box, scale)
        size = (x1 - x0, y1 - y0)
        if kind == "keep":
            continue
        if kind == "copy":
            sx, sy = (v * scale for v in edit["from"])
            out.paste(texture.hires.crop((sx, sy, sx + size[0], sy + size[1])), (x0, y0))
        elif kind == "clear":
            out.paste(clear, (x0, y0, x1, y1))
        elif kind == "label":
            out.paste(clear, (x0, y0, x1, y1))
            out.alpha_composite(_lettering(size, edit["label"], font, STYLES[edit["style"]], scale), (x0, y0))
        elif kind == "relabel":
            region = texture.hires.crop((x0, y0, x1, y1))
            letter = _letter_mask(region)
            ink = _darkest(region, letter)
            blank = _fill(region, _grown(letter, scale // 4))
            # The new lettering is as tall as the old letter and fits the box's width.
            _, top, _, bottom = letter.getbbox() or (0, 0, 0, size[1])
            text = _lettering((size[0] - scale, bottom - top), edit["relabel"], font, ink, 0)
            blank.alpha_composite(text, (scale // 2, top))
            out.paste(blank, (x0, y0))
        elif kind == "shell":
            name, sx, sy = edit["from"]
            source = others[name]
            factor = source.scale
            region = source.hires.crop((sx * factor, sy * factor, sx * factor + (box[2] - box[0]) * factor,
                                        sy * factor + (box[3] - box[1]) * factor)).resize(size, Image.LANCZOS)
            # The letter's outline is about one vanilla pixel wide.
            out.paste(_fill(region, _grown(_yellow_mask(region), scale)), (x0, y0))
        elif kind == "glyph":
            if edit.get("inlay", False):
                out.paste(_inlay_ground(texture.hires, (x0, y0, x1, y1), scale), (x0, y0))
            elif not edit.get("badge", False):
                out.paste(clear, (x0, y0, x1, y1))
            out.alpha_composite(_glyph(size, glyphs / f"{edit['glyph']}.svg", edit.get("dimmed", False)), (x0, y0))
        elif kind == "chord":
            held, pressed = edit["chord"]
            if not edit.get("badge", False):
                out.paste(clear, (x0, y0, x1, y1))
            out.alpha_composite(_chord(size, glyphs / f"{held}.svg", glyphs / f"{pressed}.svg"), (x0, y0))
        elif kind == "dpad":
            colour = _dominant(texture.modded.crop(tuple(box)))
            out.paste(clear, (x0, y0, x1, y1))
            out.alpha_composite(_dpad(size, colour, scale), (x0, y0))
    stray = ImageChops.subtract(_edited(texture), covered).getbbox()
    if stray:
        raise ValueError(f"the base mod edited {stray} of this texture, which its edit table does not cover")
    return out


def _per_language(document: dict) -> dict[str, list[dict]]:
    """The table's textures; a path with {language} stands for one per entry of "languages"."""
    edits = {}
    for path, cells in document.items():
        if path in ("about", "languages"):
            continue
        for language in document.get("languages", [""]) if "{language}" in path else [""]:
            edits[path.replace("{language}", language)] = cells
    return edits


def build(romfs: ctr.Romfs, base_romfs: Path | None, pack: TexturePack, table: Path, font: Path,
          glyphs: Path) -> dict[str, bytes]:
    """Pack file name -> PNG for every texture in the edit table."""
    document = json.loads(table.read_text())
    edits = _per_language(document)

    def base(path: str) -> bytes:
        return (base_romfs / path).read_bytes()

    textures = {}
    for path in edits:
        vanilla, gl_format, data = ctxb.image(_bundle(romfs.read, path))
        modded, modded_format, modded_data = vanilla, gl_format, data
        if base_romfs and (base_romfs / _container(path)).exists():
            modded, modded_format, modded_data = ctxb.image(_bundle(base, path))
        if modded_format != gl_format or modded.size != vanilla.size:
            raise ValueError(f"{path}: the base mod changed the texture's format or size")
        hires = pack.replacement(vanilla, data, gl_format)
        if hires is None:
            raise ValueError(f"{path}: the texture pack has no copy of the vanilla texture")
        textures[path] = Texture(vanilla, modded, modded_data, gl_format, hires)
    out = {}
    for path, texture in textures.items():
        try:
            image = compose(texture, edits[path], textures, font, glyphs)
        except ValueError as error:
            raise ValueError(f"{path}: {error}") from error
        buffer = io.BytesIO()
        image.save(buffer, "PNG")
        out[pack.name(texture.modded, texture.modded_data, texture.gl_format)] = buffer.getvalue()
    return out
