"""A game's own text made for the PC: its bitmap font drawn again from an outline font, in the
font file for the game and as texture-pack images at the pack's scale, and the prompt words
of its messages reworded for our inputs. One table per game, sets/text/<game>.json:

- "font": the game's font file, the outline font (sets/fonts/), its weight and the scale of
  the pack's images. The image's name is the hash of the texture the game makes of the font
  (OoT3D: its static glyph texture, patch/oot3d/font_atlas.cpp; MM3D: each sheet).
- "messages": one table a language, applied in order: the message file, the language (QM:
  its index in the file; GMSG files hold one each) and "edits", a file beside the table with
  the language's own edits: each a regular expression over the message's markup (qm.py,
  gmsg.py) and what to put; "only" or "except" name messages by number. An edit that changes
  nothing is an error."""

from __future__ import annotations

import io
import json
import re
from collections.abc import Callable
from pathlib import Path

from PIL import Image

from z3dmods import gmsg, gzf, pica_texture, qbf, qm, text_font

FONTS = "fonts"


def _png(alpha: Image.Image) -> bytes:
    """A 4-bit alpha texture's replacement: black under the glyphs' coverage, as the game's decodes."""
    blank = Image.new("L", alpha.size)
    buffer = io.BytesIO()
    Image.merge("RGBA", (blank, blank, blank, alpha)).save(buffer, "PNG")
    return buffer.getvalue()


def _name(alpha: Image.Image, new_hash: bool) -> str:
    """The pack's file name for a 4-bit alpha texture."""
    blank = Image.new("L", alpha.size)
    decoded = Image.merge("RGBA", (blank, blank, blank, alpha))
    return pica_texture.pack_name(decoded, pica_texture.encode_a4(alpha), pica_texture.A4, new_hash)


def _draw(outline: text_font.Outline, cells: dict[int, text_font.Cell], scale: int):
    """Every glyph of the bitmap font with an advance: drawn from the outline, or its own cell
    enlarged where the outline font lacks the character."""
    small, large = {}, {}
    for code, cell in cells.items():
        drawn = outline.draw(code) if cell.advance else None
        if drawn is None:
            large[code] = text_font.enlarged(cell.image, scale)
        else:
            small[code] = (drawn.left, drawn.advance, drawn.small)
            large[code] = drawn.large
    return small, large


def _qbf_font(data: bytes, outline_path: Path, weight: int | None, scale: int, new_hash: bool):
    font = qbf.Font.parse(data)
    cells = {g.code: text_font.Cell(g.left, g.advance, font.cells[g.cell]) for g in font.glyphs
             if g.cell < len(font.cells)}
    outline = text_font.Outline(outline_path, weight, cells, scale)
    small, large = _draw(outline, cells, scale)
    redrawn = font.redrawn(small)
    by_cell = {g.cell: large[g.code] for g in font.glyphs if g.code in large}
    blank = Image.new("L", (font.cell_width * scale, font.cell_height * scale))
    big = tuple(by_cell.get(i, blank) for i in range(len(font.cells)))
    texture = qbf.static_texture(redrawn)
    return redrawn.build(), {_name(texture, new_hash): _png(qbf.static_texture(redrawn, big))}, outline.fit()


def _gzf_font(data: bytes, outline_path: Path, weight: int | None, scale: int, new_hash: bool):
    font = gzf.Font.parse(data)
    cells = {g.code: text_font.Cell(g.left, g.advance, font.cell(g)) for g in font.glyphs}
    outline = text_font.Outline(outline_path, weight, cells, scale)
    small, large = _draw(outline, cells, scale)
    redrawn = font.redrawn(small)
    sheets = [Image.new("L", (sheet.width * scale, sheet.height * scale)) for sheet in font.sheets]
    for glyph in font.glyphs:
        sheets[glyph.sheet].paste(large[glyph.code], font.box(glyph, scale)[:2])
    textures = {_name(sheet, new_hash): _png(big) for sheet, big in zip(redrawn.sheets, sheets)}
    return redrawn.build(), textures, outline.fit()


def _edit(markup: str, number: int, edits: list[dict], used: list[int]) -> str:
    for index, edit in enumerate(edits):
        name = f"{number:04X}"
        if name in edit.get("except", ()) or ("only" in edit and name not in edit["only"]):
            continue
        markup, count = re.subn(edit["find"], edit["put"], markup)
        used[index] += count
    return markup


def _check(edits: list[dict], used: list[int]) -> None:
    for edit, count in zip(edits, used):
        if not count:
            raise ValueError(f"the edit {edit['find']!r} changes no message")


def _qm_messages(data: bytes, language: int, edits: list[dict]) -> tuple[bytes, int]:
    messages = qm.Messages.parse(data)
    used = [0] * len(edits)
    changed = 0
    for number, _, texts in messages.entries:
        text = texts.get(language)
        if text is None:
            continue
        markup = qm.to_markup(text)
        edited = _edit(markup, number, edits, used)
        if edited != markup:
            if qm.from_markup(markup) != text:
                raise ValueError(f"message 0x{number:04X} does not read back from its markup")
            texts[language] = qm.from_markup(edited)
            changed += 1
    _check(edits, used)
    return messages.build(), changed


def _gmsg_messages(data: bytes, edits: list[dict]) -> tuple[bytes, int]:
    messages = gmsg.Messages.parse(data)
    used = [0] * len(edits)
    changed = 0
    for index, (number, rest, text) in enumerate(messages.entries):
        try:
            markup = gmsg.to_markup(text)
        except (UnicodeDecodeError, ValueError):
            continue
        edited = _edit(markup, number, edits, used)
        if edited != markup:
            if gmsg.from_markup(markup) != text:
                raise ValueError(f"message 0x{number:04X} does not read back from its markup")
            messages.entries[index] = (number, rest, gmsg.from_markup(edited))
            changed += 1
    _check(edits, used)
    return messages.build(), changed


def build(read: Callable[[str], bytes], table: Path, new_hash: bool) -> tuple[dict[str, bytes], dict[str, bytes]]:
    """The game's files to replace (RomFS path -> bytes) and the pack's images (file name -> PNG).
    `read` gives a file of the game as the set has it."""
    document = json.loads(table.read_text())
    files: dict[str, bytes] = {}
    textures: dict[str, bytes] = {}
    font = document.get("font")
    if font:
        make = {".qbf": _qbf_font, ".gzf": _gzf_font}[Path(font["file"]).suffix]
        files[font["file"]], textures, fit = make(read(font["file"]), table.parent.parent / FONTS / font["outline"],
                                                  font.get("weight"), font["scale"], new_hash)
        print(f"{font['file']}: {font['outline']} at {fit}")
    for messages in document.get("messages", []):
        path = messages["file"]
        edits = json.loads((table.parent / messages["edits"]).read_text())["edits"]
        data = files[path] if path in files else read(path)
        try:
            if path.endswith(".qm"):
                files[path], changed = _qm_messages(data, messages["language"], edits)
            else:
                files[path], changed = _gmsg_messages(data, edits)
        except ValueError as error:
            raise ValueError(f"{messages['edits']}: {error}") from error
        print(f"{path}: {changed} messages reworded by {messages['edits']}")
    return files, textures
