"""Apply BPS patches (beat format) and read their source/target checksums."""

from __future__ import annotations

import struct
import zlib
from dataclasses import dataclass


@dataclass(frozen=True)
class BpsInfo:
    source_size: int
    target_size: int
    source_crc: int
    target_crc: int


def _number(data: bytes, pos: int) -> tuple[int, int]:
    value, shift = 0, 1
    while True:
        byte = data[pos]
        pos += 1
        value += (byte & 0x7F) * shift
        if byte & 0x80:
            return value, pos
        shift <<= 7
        value += shift


def info(patch: bytes) -> BpsInfo:
    if patch[:4] != b"BPS1":
        raise ValueError("not a BPS patch")
    source_size, pos = _number(patch, 4)
    target_size, pos = _number(patch, pos)
    source_crc, target_crc, _ = struct.unpack_from("<III", patch, len(patch) - 12)
    return BpsInfo(source_size, target_size, source_crc, target_crc)


def apply(patch: bytes, source: bytes) -> bytes:
    meta = info(patch)
    if zlib.crc32(patch[:-4]) != struct.unpack_from("<I", patch, len(patch) - 4)[0]:
        raise ValueError("bps: patch checksum mismatch")
    if len(source) != meta.source_size or zlib.crc32(source) != meta.source_crc:
        raise ValueError("bps: source does not match the patch")
    pos = 4
    _, pos = _number(patch, pos)
    _, pos = _number(patch, pos)
    metadata_size, pos = _number(patch, pos)
    pos += metadata_size
    target = bytearray(meta.target_size)
    out = source_rel = target_rel = 0
    end = len(patch) - 12
    while pos < end:
        word, pos = _number(patch, pos)
        action, length = word & 3, (word >> 2) + 1
        if action == 0:
            target[out:out + length] = source[out:out + length]
        elif action == 1:
            target[out:out + length] = patch[pos:pos + length]
            pos += length
        else:
            delta, pos = _number(patch, pos)
            offset = (-1 if delta & 1 else 1) * (delta >> 1)
            if action == 2:
                source_rel += offset
                target[out:out + length] = source[source_rel:source_rel + length]
                source_rel += length
            else:
                target_rel += offset
                for i in range(length):
                    target[out + i] = target[target_rel + i]
                target_rel += length
        out += length
    if zlib.crc32(target) != meta.target_crc:
        raise ValueError("bps: target checksum mismatch")
    return bytes(target)
