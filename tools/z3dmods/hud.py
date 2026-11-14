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

A texture whose prompt cells each name the app's key binding for that prompt (`"key"`: "<table> <name>"
lines of `z3d --keys`, several for one cell) also gets a keyboard image: each cell a key cap labelled
with the app's key, or the mouse button. The core shows it while the player last used the keyboard
(the pack's `variants/keyboard/`).
"""

from __future__ import annotations

import io
import json
from dataclasses import dataclass
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

from typing import TYPE_CHECKING

from z3dmods import ctr, ctxb, gar, glyphs, pica_texture
from z3dmods.texture_pack import TexturePack

if TYPE_CHECKING:
    from z3dmods.app_keys import AppKeys

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
    glyph = glyphs.image(svg).resize((min(size), min(size)), Image.LANCZOS)
    if dimmed:
        r, g, b, a = glyph.split()
        glyph = Image.merge("RGBA", (*(c.point(lambda v: int(v * DIMMED)) for c in (r, g, b)), a))
    layer = Image.new("RGBA", size, (0, 0, 0, 0))
    layer.alpha_composite(glyph, ((size[0] - glyph.width) // 2, (size[1] - glyph.height) // 2))
    return layer


# port-assets' key caps beside the pad glyphs, a mouse button's cap by SDL's number, and the
# label's share of the cap: its capitals' height, and the most of the width it takes.
KEY_SET = "keyboard"
MOUSE_CAPS = {"1": "mouse_left", "2": "mouse_middle", "3": "mouse_right"}
CAP_LABEL_HEIGHT = 0.45
CAP_LABEL_WIDTH = 0.7
CAP_INK = (40, 40, 40)


def _shift_symbol(side: int) -> Image.Image:
    """The keyboard's shift legend, an upward arrow, `side` pixels square."""
    big = side * SUPERSAMPLE
    arrow = [(0.5, 0.0), (1.0, 0.5), (0.72, 0.5), (0.72, 1.0), (0.28, 1.0), (0.28, 0.5), (0.0, 0.5)]
    layer = Image.new("RGBA", (big, big), (0, 0, 0, 0))
    ImageDraw.Draw(layer).polygon([(x * big, y * big) for x, y in arrow], fill=(*CAP_INK, 255))
    return layer.resize((side, side), Image.LANCZOS)


def _backspace_symbol(side: int) -> Image.Image:
    """The keyboard's backspace legend, a left-pointing tag with a cross cut out, `side` pixels square."""
    big = side * SUPERSAMPLE
    tag = [(0.0, 0.5), (0.32, 0.12), (1.0, 0.12), (1.0, 0.88), (0.32, 0.88)]
    layer = Image.new("RGBA", (big, big), (0, 0, 0, 0))
    draw = ImageDraw.Draw(layer)
    draw.polygon([(x * big, y * big) for x, y in tag], fill=(*CAP_INK, 255))
    stroke = max(1, big // 9)
    for start, end in (((0.45, 0.3), (0.82, 0.7)), ((0.45, 0.7), (0.82, 0.3))):
        draw.line([(start[0] * big, start[1] * big), (end[0] * big, end[1] * big)], fill=(0, 0, 0, 0), width=stroke)
    return layer.resize((side, side), Image.LANCZOS)


# Keys whose names are too long for a cell, drawn as the legend keyboards print on them.
KEY_SYMBOLS = {"Left Shift": _shift_symbol, "Right Shift": _shift_symbol, "Backspace": _backspace_symbol}
# Keys whose names are too long for a cell, by the short name keyboards print.
KEY_SHORT = {"Escape": "Esc"}
# A symbol's share of the cap's height.
CAP_SYMBOL = 0.55
# A cell of four one-letter keys (WASD, the D-pad's slots) letters them in two rows: each row's
# share of the cap's height and the gap between them.
FOUR_KEYS = 4
CAP_ROW_HEIGHT = 0.3
CAP_ROW_GAP = 0.06


def _key_cap(size: tuple[int, int], cell: dict, keys: AppKeys, glyph_set: Path, font: Path) -> Image.Image:
    """The cap of the app's key for a prompt cell, or its mouse button, as large as fits `size`.
    A slot behind the chord is its key alone: the text names the chord with a cell of its own."""
    bindings = [line.split(" ") for line in cell["key"]]
    caps = glyph_set.parent / KEY_SET
    dimmed = cell.get("dimmed", False)
    if keys.is_mouse(bindings[0][0]):
        return _glyph(size, caps / f"{MOUSE_CAPS[keys.key(*bindings[0])]}.svg", dimmed)
    layer = _glyph(size, caps / "cap.svg", dimmed)
    names = [KEY_SHORT.get(key, key) for key in (keys.key(table, name).split("+")[-1] for table, name in bindings)]
    label = "".join(names)
    side = min(size)
    if len(names) == FOUR_KEYS and all(len(name) == 1 for name in names):
        # Four letters in a row are too small to read in a cell; two rows of two are not.
        row = (round(side * CAP_LABEL_WIDTH), round(side * CAP_ROW_HEIGHT))
        for index, text in enumerate((label[:2], label[2:])):
            lettering = _lettering(row, text, font, CAP_INK, 0)
            if dimmed:
                lettering.putalpha(lettering.getchannel("A").point(lambda v: int(v * DIMMED)))
            top = size[1] // 2 - row[1] - round(side * CAP_ROW_GAP / 2) if index == 0 else size[1] // 2 + round(side * CAP_ROW_GAP / 2)
            layer.alpha_composite(lettering, ((size[0] - row[0]) // 2, top))
        return layer
    if label in KEY_SYMBOLS:
        symbol = KEY_SYMBOLS[label](round(side * CAP_SYMBOL))
        if dimmed:
            symbol.putalpha(symbol.getchannel("A").point(lambda v: int(v * DIMMED)))
        layer.alpha_composite(symbol, ((size[0] - symbol.width) // 2, (size[1] - symbol.height) // 2))
        return layer
    room = (round(side * CAP_LABEL_WIDTH), round(side * CAP_LABEL_HEIGHT))
    text = _lettering(room, label, font, CAP_INK, 0)
    if dimmed:
        text.putalpha(text.getchannel("A").point(lambda v: int(v * DIMMED)))
    layer.alpha_composite(text, ((size[0] - room[0]) // 2, (size[1] - room[1]) // 2))
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


def _face_buttons(size: tuple[int, int], glyph_set: Path, names: list[str]) -> Image.Image:
    """Four glyphs as the pad's face buttons sit: `names` top, left, right, bottom."""
    layer = Image.new("RGBA", size, (0, 0, 0, 0))
    half = (size[0] // 2, size[1] // 2)
    spots = ((size[0] // 4, 0), (0, size[1] // 4), (size[0] - half[0], size[1] // 4),
             (size[0] // 4, size[1] - half[1]))
    for name, spot in zip(names, spots, strict=True):
        layer.alpha_composite(_glyph(half, glyph_set / f"{name}.svg", False), spot)
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
            glyphs: Path, keys: AppKeys | None = None) -> Image.Image:
    """The pack's vanilla copy with the mod's edits redone at its scale; with `keys`, each
    prompt cell is the cap of its key."""
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
            if keys is not None:
                out.alpha_composite(_key_cap(size, edit, keys, glyphs, font), (x0, y0))
            elif isinstance(edit["glyph"], list):
                out.alpha_composite(_face_buttons(size, glyphs, edit["glyph"]), (x0, y0))
            else:
                out.alpha_composite(_glyph(size, glyphs / f"{edit['glyph']}.svg", edit.get("dimmed", False)),
                                    (x0, y0))
        elif kind == "chord":
            held, pressed = edit["chord"]
            if not edit.get("badge", False):
                out.paste(clear, (x0, y0, x1, y1))
            if keys is not None and "key" in edit:
                out.alpha_composite(_key_cap(size, edit, keys, glyphs, font), (x0, y0))
            else:
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


def _keyed(cells: list[dict]) -> bool:
    """Whether a texture's prompt cells name their keys; some but not all is a table error."""
    prompts = [cell for cell in cells if "glyph" in cell]
    named = [cell for cell in prompts if "key" in cell]
    if named and len(named) != len(prompts):
        raise ValueError("some of its prompt cells name a key and some do not")
    return bool(named)


def _png(image: Image.Image) -> bytes:
    buffer = io.BytesIO()
    image.save(buffer, "PNG")
    return buffer.getvalue()


def build(romfs: ctr.Romfs, base_romfs: Path | None, pack: TexturePack, table: Path, font: Path,
          glyphs: Path, keys: AppKeys) -> tuple[dict[str, bytes], dict[str, bytes]]:
    """Pack file name -> PNG for every texture in the edit table, and for the keyboard images of
    those whose prompts name keys."""
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
    out, keyboard = {}, {}
    for path, texture in textures.items():
        name = pack.name(texture.modded, texture.modded_data, texture.gl_format)
        try:
            out[name] = _png(compose(texture, edits[path], textures, font, glyphs))
            if _keyed(edits[path]):
                keyboard[name] = _png(compose(texture, edits[path], textures, font, glyphs, keys))
        except ValueError as error:
            raise ValueError(f"{path}: {error}") from error
    return out, keyboard
