"""OoT3D room files (.zsi): header commands and the room's CMB models.

Header offsets count from 0x10, the end of the file header. Command layout after noclip.website's
src/OcarinaOfTime3D/zsi.ts (MIT, Jasper St. Pierre and contributors).
"""

from __future__ import annotations

import struct
from dataclasses import dataclass

MAGIC = b"ZSI\x01"
BASE = 0x10
END = 0x14
MESH = 0x0A
MULTI_SETUP = 0x18
# Commands whose word is an offset into the file.
POINTERS = {0x01, MESH, 0x0B, MULTI_SETUP}
MESH_ENTRY_MODELS = (0x08, 0x0C)
CMB_MAGIC = b"cmb "
CMB_SIZE = 0x04


@dataclass(frozen=True)
class Room:
    data: bytes
    # File offsets of every header command, main setup and alternates.
    commands: list[int]
    # File offsets of every mesh entry's model pointer that is set.
    model_pointers: list[int]

    @classmethod
    def parse(cls, data: bytes) -> Room:
        if data[0:4] != MAGIC:
            raise ValueError("not an OoT3D room file")
        commands: list[int] = []
        pointers: list[int] = []
        lists = [BASE]
        seen: set[int] = set()
        while lists:
            offset = lists.pop()
            if offset in seen:
                continue
            seen.add(offset)
            while True:
                code, count, word = struct.unpack_from("<BBxxI", data, offset)
                commands.append(offset)
                if code == END:
                    break
                if code == MULTI_SETUP:
                    for i in range(count):
                        setup = struct.unpack_from("<I", data, BASE + word + i * 4)[0]
                        if setup:
                            lists.append(BASE + setup)
                if code == MESH:
                    pointers.extend(_model_pointers(data, BASE + word))
                offset += 8
        return cls(data, commands, sorted(set(pointers)))

    def models(self) -> list[int]:
        """File offsets of the room's CMB models."""
        offsets = (BASE + struct.unpack_from("<I", self.data, p)[0] for p in self.model_pointers)
        return [o for o in offsets if self.data[o:o + 4] == CMB_MAGIC]

    def with_model(self, model: int, cmb: bytes) -> bytes:
        """The room with the CMB at a file offset replaced; everything after it moves."""
        old_size = struct.unpack_from("<I", self.data, model + CMB_SIZE)[0]
        end = model - BASE + old_size
        grow = len(cmb) - old_size
        out = bytearray(self.data[:model] + cmb + self.data[model + old_size:])

        locations: set[int] = set()
        for command in self.commands:
            code, count, word = struct.unpack_from("<BBxxI", self.data, command)
            if code in POINTERS:
                locations.add(command + 4)
            if code == MULTI_SETUP:
                locations.update(BASE + word + i * 4 for i in range(count))
            if code == MESH:
                locations.add(BASE + word + 4)
        locations.update(self.model_pointers)
        for at in locations:
            value = struct.unpack_from("<I", self.data, at)[0]
            if value >= end:
                struct.pack_into("<I", out, at + (grow if at >= model + old_size else 0), value + grow)
        return bytes(out)


def _model_pointers(data: bytes, header: int) -> list[int]:
    count = data[header + 1]
    entries = BASE + struct.unpack_from("<I", data, header + 4)[0]
    if count != 1:
        raise ValueError(f"mesh header with {count} entries")
    return [entries + field for field in MESH_ENTRY_MODELS
            if struct.unpack_from("<I", data, entries + field)[0]]
