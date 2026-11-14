"""The item icons the app's play HUD draws (app/src/hud.hpp): cut from the game's own icon sheet,
or the texture pack's copy of it, by the game's table of an icon frame per item id.

`patch/<game>/hud.toml` names them under `[icons]`: `sheet` (a romfs CTXB, `<archive>.gar/<file>`),
`frames` (the decomp's table global), `count` and `none` (its length and its "no icon" value),
`frame` (a frame's size in the sheet's own pixels), `columns` and `icons` (how many frames are
icons; the sheet holds other art after them)."""

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


def build(repo: Path, game: str, romfs: ctr.Romfs, pack: TexturePack | None, code: bytes) -> dict[int, bytes]:
    """Item id -> PNG; nothing for a game whose hud.toml has no `[icons]`."""
    path = repo / "patch" / game / "hud.toml"
    table = tomllib.loads(path.read_text()).get("icons") if path.exists() else None
    if table is None:
        return {}
    decomp = symbols.load(repo, game)
    archive, _, inside = table["sheet"].partition(".gar/")
    vanilla, gl_format, data = ctxb.image(gar.files(romfs.read(archive + ".gar"))[inside])
    sheet = (pack.replacement(vanilla, data, gl_format) if pack else None) or vanilla
    scale = sheet.width / vanilla.width
    width, height = table["frame"]
    start = symbols.global_(decomp, table["frames"]) - IMAGE_BASE
    frames = code[start:start + decomp.define(table["count"])]
    icons = {}
    for item, frame in enumerate(frames):
        if frame == decomp.define(table["none"]) or frame >= table["icons"]:
            continue
        left = frame % table["columns"] * width
        top = frame // table["columns"] * height
        box = tuple(round(side * scale) for side in (left, top, left + width, top + height))
        icon = sheet.crop(box)
        icon.thumbnail((LARGEST, LARGEST), Image.Resampling.LANCZOS)
        buffer = io.BytesIO()
        icon.save(buffer, "PNG")
        icons[item] = buffer.getvalue()
    return icons
