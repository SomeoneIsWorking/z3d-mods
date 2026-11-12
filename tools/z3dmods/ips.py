"""IPS patches as used for 3DS code.bin: 24-bit offsets, RLE records, optional truncate."""

from __future__ import annotations

import struct
from dataclasses import dataclass


@dataclass(frozen=True)
class Record:
    offset: int
    data: bytes


def parse(patch: bytes) -> list[Record]:
    if patch[:5] != b"PATCH":
        raise ValueError("not an IPS patch")
    records = []
    pos = 5
    while patch[pos:pos + 3] != b"EOF":
        offset = int.from_bytes(patch[pos:pos + 3], "big")
        size = struct.unpack_from(">H", patch, pos + 3)[0]
        pos += 5
        if size:
            records.append(Record(offset, patch[pos:pos + size]))
            pos += size
        else:
            run, value = struct.unpack_from(">HB", patch, pos)
            records.append(Record(offset, bytes([value]) * run))
            pos += 3
    return records


def apply(patch: bytes, code: bytes) -> bytes:
    out = bytearray(code)
    for record in parse(patch):
        end = record.offset + len(record.data)
        if end > len(out):
            out.extend(bytes(end - len(out)))
        out[record.offset:end] = record.data
    return bytes(out)


def build(records: list[Record]) -> bytes:
    out = bytearray(b"PATCH")
    for record in records:
        for start in range(0, len(record.data), 0xFFFF):
            chunk = record.data[start:start + 0xFFFF]
            offset = record.offset + start
            if offset >= 1 << 24:
                raise ValueError(f"ips: offset 0x{offset:X} past 24 bits")
            if offset == 0x454F46:
                raise ValueError("ips: offset collides with the EOF marker")
            out += offset.to_bytes(3, "big") + struct.pack(">H", len(chunk)) + chunk
    return bytes(out + b"EOF")
