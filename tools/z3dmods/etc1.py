"""3DS ETC1 and ETC1A4 textures to RGBA rows, and back.

PICA200 stores 4x4 ETC1 blocks as little-endian 64-bit words, ordered in 8x8 tiles of four blocks
(left to right, top to bottom); ETC1A4 puts a 64-bit word of 4-bit alphas before each block.
"""

from __future__ import annotations

import struct

MODIFIERS = ((2, 8), (5, 17), (9, 29), (13, 42), (18, 60), (24, 80), (33, 106), (47, 183))
ETC1 = 0x675A
ETC1A4 = 0x675B


def _clamp(value: int) -> int:
    return 0 if value < 0 else 255 if value > 255 else value


def _block(word: int) -> list[tuple[int, int, int]]:
    """Sixteen RGB texels, index x * 4 + y as ETC1 orders them."""
    flip = (word >> 32) & 1
    diff = (word >> 33) & 1
    tables = ((word >> 37) & 7, (word >> 34) & 7)
    if diff:
        base1 = [(word >> shift) & 0x1F for shift in (59, 51, 43)]
        delta = [(word >> shift) & 7 for shift in (56, 48, 40)]
        base2 = [(b + (d - 8 if d & 4 else d)) & 0x1F for b, d in zip(base1, delta)]
        colors = ([(c << 3) | (c >> 2) for c in base1], [(c << 3) | (c >> 2) for c in base2])
    else:
        colors = ([((word >> s) & 0xF) * 17 for s in (60, 52, 44)],
                  [((word >> s) & 0xF) * 17 for s in (56, 48, 40)])
    out = []
    for x in range(4):
        for y in range(4):
            sub = (y >= 2) if flip else (x >= 2)
            bit = x * 4 + y
            low = (word >> bit) & 1
            high = (word >> (bit + 16)) & 1
            modifier = MODIFIERS[tables[sub]][low]
            if high:
                modifier = -modifier
            out.append(tuple(_clamp(c + modifier) for c in colors[sub]))
    return out


def decode(data: bytes, width: int, height: int, alpha: bool) -> list[list[tuple[int, int, int, int]]]:
    rows = [[(0, 0, 0, 255)] * width for _ in range(height)]
    offset = 0
    for tile_y in range(0, height, 8):
        for tile_x in range(0, width, 8):
            for block in range(4):
                bx = tile_x + (block & 1) * 4
                by = tile_y + (block >> 1) * 4
                alphas = 0xFFFFFFFFFFFFFFFF
                if alpha:
                    alphas = struct.unpack_from("<Q", data, offset)[0]
                    offset += 8
                word = struct.unpack_from("<Q", data, offset)[0]
                offset += 8
                for index, rgb in enumerate(_block(word)):
                    x, y = index // 4, index % 4
                    a = ((alphas >> (index * 4)) & 0xF) * 17
                    rows[by + y][bx + x] = (*rgb, a)
    return rows


def _encode_block(texels: list[tuple[int, int, int]]) -> int:
    """Sixteen RGB texels, index x * 4 + y, as one ETC1 word in individual mode: for each split,
    each half's mean in 4 bits and the table that fits it best."""
    best = None
    for flip in (0, 1):
        word = flip << 32
        error = 0
        for sub in (0, 1):
            members = [i for i in range(16) if ((i % 4 >= 2) if flip else (i // 4 >= 2)) == bool(sub)]
            base = [min(15, max(0, round(sum(texels[i][c] for i in members) / len(members) / 17))) for c in range(3)]
            for c, shift in enumerate((60, 52, 44) if sub == 0 else (56, 48, 40)):
                word |= base[c] << shift
            base = [b * 17 for b in base]
            choice = None
            for table, (small, large) in enumerate(MODIFIERS):
                options = ((small, 0, 0), (large, 1, 0), (-small, 0, 1), (-large, 1, 1))
                bits = 0
                total = 0
                for i in members:
                    found = min(options, key=lambda o: sum((_clamp(b + o[0]) - t) ** 2 for b, t in zip(base, texels[i])))
                    total += sum((_clamp(b + found[0]) - t) ** 2 for b, t in zip(base, texels[i]))
                    bits |= found[1] << i | found[2] << (i + 16)
                if choice is None or total < choice[0]:
                    choice = (total, table, bits)
            error += choice[0]
            word |= choice[1] << (37 if sub == 0 else 34) | choice[2]
        if best is None or error < best[0]:
            best = (error, word)
    return best[1]


def encode(pixels: list[tuple[int, int, int, int]], width: int, height: int, alpha: bool) -> bytes:
    """RGBA texels, rows top first, as an ETC1 texture, or ETC1A4 with each alpha in 4 bits."""
    out = bytearray()
    for tile_y in range(0, height, 8):
        for tile_x in range(0, width, 8):
            for block in range(4):
                bx = tile_x + (block & 1) * 4
                by = tile_y + (block >> 1) * 4
                texels = [pixels[(by + index % 4) * width + bx + index // 4] for index in range(16)]
                if alpha:
                    out += struct.pack("<Q", sum(((t[3] + 8) // 17) << (index * 4) for index, t in enumerate(texels)))
                out += struct.pack("<Q", _encode_block([t[:3] for t in texels]))
    return bytes(out)
