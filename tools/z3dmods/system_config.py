"""The 3DS system settings a user folder's NAND holds (the CFG module's config save), as the core
reads them at boot: a u16 count of blocks and a u16 data offset, then a 12-byte entry a block
(u32 id, u32 data or its offset, u16 size, u16 flags), a block of four bytes or fewer held in
the entry itself. Only the system language is written, for runs in another language."""

from __future__ import annotations

import struct
from pathlib import Path

CONFIG = Path("nand/data/00000000000000000000000000000000/sysdata/00010017/00000000/config")
HEADER = struct.Struct("<HH")
ENTRY = struct.Struct("<IIHH")
LANGUAGE_BLOCK = 0x000A0002
# The CFG module's numbers of the languages a USA title offers.
LANGUAGES = {"en": 1, "fr": 2, "es": 5}


def set_language(azahar: Path, language: str) -> None:
    """Sets the system language of the user folder whose Azahar/ folder is `azahar`."""
    path = azahar / CONFIG
    data = bytearray(path.read_bytes())
    count, _ = HEADER.unpack_from(data)
    for index in range(count):
        at = HEADER.size + index * ENTRY.size
        block, _, size, flags = ENTRY.unpack_from(data, at)
        if block == LANGUAGE_BLOCK:
            ENTRY.pack_into(data, at, block, LANGUAGES[language], size, flags)
            path.write_bytes(bytes(data))
            return
    raise ValueError(f"{path} has no system language block")
