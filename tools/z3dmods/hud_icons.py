"""The game art the app's play HUD draws (app/src/hud.hpp): item icons cut from the game's own icon
sheet, or the texture pack's copy of it, by the game's table of an icon frame per item id, and
named pieces (the rupee, the key, the hearts) cut from the HUD's sheet the same way.

`patch/<game>/hud.toml` names them under `[icons]`: `sheet` (a romfs CTXB, or `<archive>.gar/<file>`),
`frame` (a frame's size in the sheet's own pixels), `columns` and `icons` (how many frames are
icons; the sheet holds other art after them). A game with a table of an icon frame per item id
names it: `frames` (the decomp's global), `count` and `none` (its length and its "no icon" value).
Without one, an item's frame is its id.

`[art]` names the HUD's sheet (`sheet`) and each piece's rectangle in the sheet's own pixels
(`<name> = [x, y, w, h]`)."""

from __future__ import annotations

import io
import tomllib
from pathlib import Path

from z3dmods import ctr, ctxb, gar, symbols
from z3dmods.texture_pack import TexturePack

from PIL import Image

# Where the loader maps the code image.
IMAGE_BASE = 0x00100000
# An icon's longest side as installed; a HUD button is never drawn larger.
LARGEST = 256


def _sheet(romfs: ctr.Romfs, pack: TexturePack | None, name: str) -> tuple[Image.Image, float]:
    """A romfs sheet, the pack's copy when it has one, and how many of its pixels one of the game's is."""
    archive, gar_mark, inside = name.partition(".gar/")
    file = gar.files(romfs.read(archive + ".gar"))[inside] if gar_mark else romfs.read(archive)
    vanilla, gl_format, data = ctxb.image(file)
    sheet = (pack.replacement(vanilla, data, gl_format) if pack else None) or vanilla
    return sheet, sheet.width / vanilla.width


def _png(sheet: Image.Image, scale: float, left: int, top: int, width: int, height: int) -> bytes:
    icon = sheet.crop(tuple(round(side * scale) for side in (left, top, left + width, top + height)))
    icon.thumbnail((LARGEST, LARGEST), Image.Resampling.LANCZOS)
    buffer = io.BytesIO()
    icon.save(buffer, "PNG")
    return buffer.getvalue()


def build(repo: Path, game: str, romfs: ctr.Romfs, pack: TexturePack | None, code: bytes) -> dict[str, bytes]:
    """File stem -> PNG: an item's id or a piece's name; nothing for a game without a hud.toml."""
    path = repo / "patch" / game / "hud.toml"
    tables = tomllib.loads(path.read_text()) if path.exists() else {}
    out = {}
    if (art := tables.get("art")) is not None:
        sheet, scale = _sheet(romfs, pack, art["sheet"])
        out.update({name: _png(sheet, scale, *box) for name, box in art.items() if name != "sheet"})
    if (table := tables.get("icons")) is not None:
        out.update(_items(repo, game, romfs, pack, code, table))
    return out


def _items(repo: Path, game: str, romfs: ctr.Romfs, pack: TexturePack | None, code: bytes,
           table: dict) -> dict[str, bytes]:
    decomp = symbols.load(repo, game)
    sheet, scale = _sheet(romfs, pack, table["sheet"])
    width, height = table["frame"]
    if "frames" in table:
        start = symbols.global_(decomp, table["frames"]) - IMAGE_BASE
        frames = code[start:start + decomp.define(table["count"])]
        none = decomp.define(table["none"])
    else:
        frames = range(table["icons"])
        none = None
    icons = {}
    for item, frame in enumerate(frames):
        if frame == none or frame >= table["icons"]:
            continue
        icons[str(item)] = _png(sheet, scale, frame % table["columns"] * width, frame // table["columns"] * height,
                                width, height)
    return icons
