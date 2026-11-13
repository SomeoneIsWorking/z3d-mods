"""CTXB texture bundles (menu and message art): a header and one CMB-style texture table."""

from __future__ import annotations

import struct

from PIL import Image

from z3dmods import cmb, pica_texture

MAGIC = b"ctxb"
TABLE = 0x10
DATA = 0x14


def image(data: bytes) -> tuple[Image.Image, int]:
    """The bundle's one texture, decoded, and its GL format."""
    if data[:4] != MAGIC:
        raise ValueError("not a CTXB bundle")
    table, base = struct.unpack_from("<II", data, TABLE)
    textures = cmb.texture_table(data, table)
    if len(textures) != 1:
        raise ValueError(f"CTXB with {len(textures)} textures")
    texture = textures[0]
    start = base + texture.offset
    return (pica_texture.decode(data[start:start + texture.size], texture.width, texture.height,
                                texture.format), texture.format)
