"""Read decrypted 3DS cartridge images: NCSD, NCCH, exheader, ExeFS and the .code section."""

from __future__ import annotations

import hashlib
import struct
from dataclasses import dataclass
from pathlib import Path

MEDIA_UNIT = 0x200


@dataclass(frozen=True)
class CodeSetInfo:
    address: int
    pages: int
    size: int


@dataclass(frozen=True)
class Exheader:
    raw: bytes
    title: str
    compressed_code: bool
    remaster_version: int
    text: CodeSetInfo
    rodata: CodeSetInfo
    data: CodeSetInfo
    bss_size: int

    @classmethod
    def parse(cls, raw: bytes) -> Exheader:
        def code_set(offset: int) -> CodeSetInfo:
            return CodeSetInfo(*struct.unpack_from("<III", raw, offset))

        return cls(
            raw=raw,
            title=raw[0:8].rstrip(b"\0").decode("ascii", "replace"),
            compressed_code=bool(raw[0xD] & 1),
            remaster_version=struct.unpack_from("<H", raw, 0xE)[0],
            text=code_set(0x10),
            rodata=code_set(0x20),
            data=code_set(0x30),
            bss_size=struct.unpack_from("<I", raw, 0x3C)[0],
        )


@dataclass(frozen=True)
class Ncch:
    image: Path
    offset: int
    program_id: int
    product_code: str
    exheader: Exheader
    exefs_offset: int
    exefs_size: int
    romfs_offset: int
    romfs_size: int

    def read(self, offset: int, size: int) -> bytes:
        with self.image.open("rb") as stream:
            stream.seek(self.offset + offset)
            return stream.read(size)

    def exefs_files(self) -> dict[str, bytes]:
        header = self.read(self.exefs_offset, 0x200)
        if header[0:8] != b".code\0\0\0":
            raise ValueError(f"{self.image}: ExeFS is encrypted or has no .code first")
        files = {}
        for index in range(10):
            name_raw, file_offset, size = struct.unpack_from("<8sII", header, index * 16)
            name = name_raw.rstrip(b"\0").decode("ascii")
            if name and size:
                files[name] = self.read(self.exefs_offset + 0x200 + file_offset, size)
        return files

    def code(self) -> bytes:
        code = self.exefs_files()[".code"]
        return lz_decompress(code) if self.exheader.compressed_code else code


def open_cci(path: Path) -> Ncch:
    with path.open("rb") as stream:
        header = stream.read(0x200)
        if header[0x100:0x104] != b"NCSD":
            raise ValueError(f"{path}: not an NCSD image")
        part_offset = struct.unpack_from("<I", header, 0x120)[0] * MEDIA_UNIT
        stream.seek(part_offset)
        ncch = stream.read(0x200)
        if ncch[0x100:0x104] != b"NCCH":
            raise ValueError(f"{path}: partition 0 is not NCCH")
        exheader = stream.read(0x800)

    def region(offset: int) -> tuple[int, int]:
        start, size = struct.unpack_from("<II", ncch, offset)
        return start * MEDIA_UNIT, size * MEDIA_UNIT

    exefs_offset, exefs_size = region(0x1A0)
    romfs_offset, romfs_size = region(0x1B0)
    return Ncch(
        image=path,
        offset=part_offset,
        program_id=struct.unpack_from("<Q", ncch, 0x118)[0],
        product_code=ncch[0x150:0x160].rstrip(b"\0").decode("ascii"),
        exheader=Exheader.parse(exheader),
        exefs_offset=exefs_offset,
        exefs_size=exefs_size,
        romfs_offset=romfs_offset,
        romfs_size=romfs_size,
    )


def lz_decompress(data: bytes) -> bytes:
    """Decompress the backward LZSS used for ExeFS .code."""
    top_and_bottom, extra = struct.unpack_from("<II", data, len(data) - 8)
    out = bytearray(data) + bytearray(extra)
    index = len(data) - (top_and_bottom >> 24)
    stop = len(data) - (top_and_bottom & 0xFFFFFF)
    out_index = len(out)
    while index > stop:
        index -= 1
        control = data[index]
        for _ in range(8):
            if index <= stop or out_index <= 0:
                break
            if control & 0x80:
                index -= 2
                pair = data[index] | (data[index + 1] << 8)
                length = ((pair >> 12) & 0xF) + 3
                distance = (pair & 0xFFF) + 2
                for _ in range(length):
                    if out_index + distance >= len(out):
                        raise ValueError("lz: back reference past end")
                    value = out[out_index + distance]
                    out_index -= 1
                    out[out_index] = value
            else:
                index -= 1
                out_index -= 1
                out[out_index] = data[index]
            control = (control << 1) & 0xFF
    return bytes(out)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()
