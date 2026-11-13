"""GAR v2 archives (MM3D's layout bundles): file name -> contents."""

from __future__ import annotations

import struct

MAGIC = b"GAR\x02"
ENTRY = 12


def _name(data: bytes, offset: int) -> str:
    return data[offset:data.index(b"\0", offset)].decode("ascii")


def files(data: bytes) -> dict[str, bytes]:
    """Every file in the archive, by the file name it was packed from."""
    if data[:4] != MAGIC:
        raise ValueError("not a GAR v2 archive")
    count, = struct.unpack_from("<H", data, 0x0A)
    entries, offsets = struct.unpack_from("<II", data, 0x10)
    out = {}
    for index in range(count):
        size, _, file_name = struct.unpack_from("<III", data, entries + index * ENTRY)
        start, = struct.unpack_from("<I", data, offsets + index * 4)
        out[_name(data, file_name)] = data[start:start + size]
    return out
