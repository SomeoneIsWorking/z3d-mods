"""Our MIT code patch for a game: built with clang/lld into an ELF, then IPS records relative to
.text. A package carries the build (`prebuilt/<game>/`: `code.ips` and `hooks.json`, the payload's
range and the base code each hook replaces), so the player's machine needs no compiler."""

from __future__ import annotations

import json
import shutil
import struct
import subprocess
from dataclasses import dataclass
from pathlib import Path

from z3dmods import ips, options, symbols

CODE_BASE = 0x00100000
SHT_PROGBITS, SHT_NOBITS, SHF_ALLOC = 1, 8, 2

CXXFLAGS = [
    "--target=armv6k-none-eabi", "-mcpu=mpcore", "-marm", "-mfloat-abi=softfp", "-mfpu=vfpv2",
    "-std=c++20", "-Os", "-ffreestanding", "-nostdinc++", "-fno-exceptions", "-fno-rtti",
    "-fno-unwind-tables", "-fno-asynchronous-unwind-tables", "-fno-threadsafe-statics",
    "-ffunction-sections", "-fdata-sections", "-Wall", "-Wextra", "-Werror",
]


@dataclass(frozen=True)
class Patch:
    records: list[ips.Record]
    # For each hook record's offset, the base code it replaces (hooks.s ".expect.<name>").
    expected: dict[int, bytes]
    payload_start: int
    payload_end: int


PREBUILT = "prebuilt"


def save(patch: Patch, folder: Path) -> None:
    """Writes a build for a package."""
    folder.mkdir(parents=True, exist_ok=True)
    (folder / "code.ips").write_bytes(ips.build(patch.records))
    hooks = {"payload": patch.payload_start, "payload_end": patch.payload_end,
             "expected": {str(offset): original.hex() for offset, original in patch.expected.items()}}
    (folder / "hooks.json").write_text(json.dumps(hooks, indent=2) + "\n")


def _load(folder: Path) -> Patch:
    hooks = json.loads((folder / "hooks.json").read_text())
    expected = {int(offset): bytes.fromhex(original) for offset, original in hooks["expected"].items()}
    return Patch(ips.parse((folder / "code.ips").read_bytes()), expected, hooks["payload"], hooks["payload_end"])


def patch(repo: Path, game: str, payload: int) -> Patch:
    """The game's patch with its payload at `payload`: the package's build where there is one,
    else built here from patch/<game>/."""
    folder = repo / PREBUILT / game
    if not folder.is_dir():
        return build(repo, game, payload, repo / "build" / "patch" / game)
    shipped = _load(folder)
    if shipped.payload_start != payload:
        raise RuntimeError(f"the shipped {game} patch is built for 0x{shipped.payload_start:X}, "
                           f"and the base mod leaves 0x{payload:X}")
    return shipped


def _tool(name: str) -> str:
    path = shutil.which(name)
    if not path:
        raise RuntimeError(f"{name} not found: install clang and lld (Fedora: dnf install clang lld)")
    return path


def _sections(elf: bytes) -> list[tuple[str, int, int, int, int, bytes]]:
    if elf[:4] != b"\x7fELF" or elf[4] != 1:
        raise ValueError("not a 32-bit ELF")
    shoff = struct.unpack_from("<I", elf, 0x20)[0]
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", elf, 0x2E)
    headers = [struct.unpack_from("<IIIIIIIIII", elf, shoff + i * shentsize) for i in range(shnum)]
    names = headers[shstrndx]
    out = []
    for name, kind, flags, addr, offset, size, *_ in headers:
        if not size:
            continue
        start = names[4] + name
        label = elf[start:elf.index(b"\0", start)].decode()
        data = elf[offset:offset + size] if kind == SHT_PROGBITS else b""
        out.append((label, kind, flags, addr, size, data))
    return out


def build(repo: Path, game: str, payload: int, build_dir: Path) -> Patch:
    source = repo / "patch" / game
    build_dir.mkdir(parents=True, exist_ok=True)
    generated = symbols.generate(repo, game, build_dir / "generated")
    options.generate(repo, game, generated.header.parent)
    objects = []
    for path in sorted(source.glob("*.cpp")) + sorted(source.glob("*.s")):
        obj = build_dir / (path.name + ".o")
        flags = [*CXXFLAGS, f"-I{generated.header.parent}"] if path.suffix == ".cpp" else CXXFLAGS[:5]
        subprocess.run([_tool("clang++"), *flags, "-c", str(path), "-o", str(obj)], check=True)
        objects.append(str(obj))
    elf_path = build_dir / f"{game}.elf"
    subprocess.run([_tool("ld.lld"), "-L", str(generated.linker.parent), "-T", str(source / "link.ld"), f"--defsym=PAYLOAD=0x{payload:X}",
                    "--gc-sections", "-o", str(elf_path), *objects], check=True)

    sections = _sections(elf_path.read_bytes())
    expects = {label.removeprefix(".expect."): data for label, kind, flags, _, _, data in sections
               if label.startswith(".expect.") and not flags & SHF_ALLOC}
    records, expected, end = [], {}, payload
    for label, kind, flags, addr, size, data in sections:
        if not flags & SHF_ALLOC:
            continue
        if addr >= payload:
            end = max(end, addr + size)
        elif not label.startswith(".patch."):
            raise RuntimeError(f"{label} at 0x{addr:08X} is below the payload and not a .patch. section")
        else:
            original = expects.get(label.removeprefix(".patch."))
            if original is None or len(original) != len(data):
                raise RuntimeError(f"{label} needs a .expect. section of {len(data)} bytes in hooks.s")
            expected[addr - CODE_BASE] = original
        if kind == SHT_PROGBITS:
            records.append(ips.Record(addr - CODE_BASE, data))
    return Patch(records, expected, payload, end)
