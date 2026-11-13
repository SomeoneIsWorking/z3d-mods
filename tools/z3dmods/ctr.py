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

    def romfs(self) -> Romfs:
        return Romfs.open(self)


NO_ENTRY = 0xFFFFFFFF


@dataclass(frozen=True)
class Romfs:
    """The RomFS file tree (IVFC level 3): every file's path and its offset in the image."""

    ncch: Ncch
    data_offset: int
    files: dict[str, tuple[int, int]]

    @classmethod
    def open(cls, ncch: Ncch) -> Romfs:
        ivfc = ncch.read(ncch.romfs_offset, 0x60)
        if ivfc[0:4] != b"IVFC":
            raise ValueError(f"{ncch.image}: RomFS is encrypted or missing")
        master_hash_size = struct.unpack_from("<I", ivfc, 0x08)[0]
        block = 1 << struct.unpack_from("<I", ivfc, 0x4C)[0]
        level3 = ncch.romfs_offset + _align(0x60 + master_hash_size, block)
        header = struct.unpack_from("<10I", ncch.read(level3, 0x28))
        dir_meta = ncch.read(level3 + header[3], header[4])
        file_meta = ncch.read(level3 + header[7], header[8])
        files: dict[str, tuple[int, int]] = {}

        def walk(dir_offset: int, prefix: str) -> None:
            _, _, child, first_file, _, name_size = struct.unpack_from("<6I", dir_meta, dir_offset)
            name = dir_meta[dir_offset + 0x18: dir_offset + 0x18 + name_size].decode("utf-16-le")
            path = f"{prefix}{name}/" if dir_offset else ""
            entry = first_file
            while entry != NO_ENTRY:
                _, sibling, data, size, _, fname_size = struct.unpack_from("<IIQQII", file_meta, entry)
                fname = file_meta[entry + 0x20: entry + 0x20 + fname_size].decode("utf-16-le")
                files[path + fname] = (data, size)
                entry = sibling
            while child != NO_ENTRY:
                walk(child, path)
                child = struct.unpack_from("<I", dir_meta, child + 4)[0]

        walk(0, "")
        return cls(ncch, level3 + header[9], files)

    def read(self, path: str) -> bytes:
        offset, size = self.files[path]
        return self.ncch.read(self.data_offset + offset, size)


def _align(value: int, alignment: int) -> int:
    return (value + alignment - 1) // alignment * alignment


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
