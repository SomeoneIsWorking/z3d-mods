"""PICA200 textures: decoding to images, and the hash Azahar names custom textures by.

Azahar's hash is CityHash64 (rasterizer_cache.h ComputeHash). A pack whose `pack.json` sets
`use_new_hash` (Henriko's MM3D pack) is named by the hash of the texture's bytes as the game holds
them. Otherwise (his OoT3D pack, or no `pack.json`) it is the hash of the texture decoded into its
own pixel format, rows bottom-up: RGBA4444 keeps its 16-bit words, ETC1 and ETC1A4 become RGBA8
(texture_codec.h MortonCopyTile).
"""

from __future__ import annotations

import struct

from PIL import Image

from z3dmods import cityhash, etc1

RGBA4 = 0x80336752
ETC1 = etc1.ETC1
ETC1A4 = etc1.ETC1A4
# Azahar's VideoCore::PixelFormat numbers, as pack file names spell them.
PACK_FORMATS = {RGBA4: 4, ETC1: 12, ETC1A4: 13}


def _morton(x: int, y: int) -> int:
    return (x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3)


def decode(data: bytes, width: int, height: int, gl_format: int) -> Image.Image:
    """The texture as an RGBA image, top row first."""
    image = Image.new("RGBA", (width, height))
    if gl_format in (ETC1, ETC1A4):
        rows = etc1.decode(data, width, height, gl_format == ETC1A4)
        image.putdata([pixel for row in rows for pixel in row])
        return image
    if gl_format != RGBA4:
        raise ValueError(f"texture format 0x{gl_format:08X} is not decoded")
    words = struct.unpack_from(f"<{width * height}H", data)
    tiles = width // 8
    pixels = []
    for y in range(height):
        for x in range(width):
            v = words[((y // 8) * tiles + x // 8) * 64 + _morton(x & 7, y & 7)]
            pixels.append((((v >> 12) & 15) * 17, ((v >> 8) & 15) * 17, ((v >> 4) & 15) * 17, (v & 15) * 17))
    image.putdata(pixels)
    return image


def decoded_hash(image: Image.Image, gl_format: int) -> int:
    rows = [image.crop((0, y, image.width, y + 1)).tobytes() for y in range(image.height - 1, -1, -1)]
    if gl_format in (ETC1, ETC1A4):
        return cityhash.hash64(b"".join(rows))
    if gl_format != RGBA4:
        raise ValueError(f"texture format 0x{gl_format:08X} is not hashed")
    raw = bytearray()
    for row in rows:
        for i in range(0, len(row), 4):
            r, g, b, a = (c // 17 for c in row[i:i + 4])
            raw += struct.pack("<H", (r << 12) | (g << 8) | (b << 4) | a)
    return cityhash.hash64(bytes(raw))


def pack_name(image: Image.Image, data: bytes, gl_format: int, new_hash: bool) -> str:
    """The file name a texture pack gives the replacement of a texture: `data`, decoded as `image`."""
    hash_ = cityhash.hash64(data) if new_hash else decoded_hash(image, gl_format)
    return f"tex1_{image.width}x{image.height}_{hash_:016X}_{PACK_FORMATS[gl_format]}_mip0.png"
