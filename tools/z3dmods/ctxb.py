"""CTXB texture bundles (menu and message art): a header and one CMB-style texture table."""

from __future__ import annotations

import struct

from PIL import Image

from z3dmods import cmb, pica_texture

MAGIC = b"ctxb"
TABLE = 0x10
DATA = 0x14


def image(data: bytes) -> tuple[Image.Image, int, bytes]:
    """The bundle's one texture, decoded, its GL format and its own bytes."""
    if data[:4] != MAGIC:
        raise ValueError("not a CTXB bundle")
    table, base = struct.unpack_from("<II", data, TABLE)
    textures = cmb.texture_table(data, table)
    if len(textures) != 1:
        raise ValueError(f"CTXB with {len(textures)} textures")
    texture = textures[0]
    start = base + texture.offset
    pixels = data[start:start + texture.size]
    return pica_texture.decode(pixels, texture.width, texture.height, texture.format), texture.format, pixels
