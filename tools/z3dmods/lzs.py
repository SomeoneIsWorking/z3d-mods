"""LzS: the LZSS wrapper around MM3D's compressed game files.

A 16-byte header ("LzS\\x01", the unpacked size at 0x08), then Okumura's LZSS with a 4096-byte
window and matches of 3 to 18 bytes. After noclip.website's src/Common/Compression/LZSS.ts (MIT,
Jasper St. Pierre and contributors).
"""

from __future__ import annotations

import struct

MAGIC = b"LzS\x01"
HEADER = 0x10
UNPACKED_SIZE = 0x08
WINDOW = 4096
LONGEST_MATCH = 18
SHORTEST_MATCH = 3


def unpacked(data: bytes) -> bytes:
    """The file's contents; a file that is not LzS is returned as it is."""
    if data[:4] != MAGIC:
        return data
    size = struct.unpack_from("<I", data, UNPACKED_SIZE)[0]
    out = bytearray()
    window = bytearray(WINDOW)
    write = WINDOW - LONGEST_MATCH
    at = HEADER
    while len(out) < size:
        flags = data[at]
        at += 1
        for bit in range(8):
            if len(out) >= size:
                break
            if flags & (1 << bit):
                window[write] = data[at]
                out.append(data[at])
                write = (write + 1) % WINDOW
                at += 1
                continue
            low, high = data[at], data[at + 1]
            at += 2
            read = low | ((high & 0xF0) << 4)
            for _ in range((high & 0x0F) + SHORTEST_MATCH):
                value = window[read]
                window[write] = value
                out.append(value)
                write = (write + 1) % WINDOW
                read = (read + 1) % WINDOW
    return bytes(out[:size])
