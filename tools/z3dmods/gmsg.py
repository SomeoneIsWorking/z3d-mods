"""GMSG, MM3D's message file (decomp/mm3d docs/message.md): a table of messages and their
text, and the text as markup.

Markup is the text with each control code as `{XX}` or `{XX:<argument>,<argument>}` in hex and
a line break (code 0x01) as a newline. A code is 0x7F, a zero where that leaves the next byte
on an odd offset, then the code and its arguments as 16-bit words."""

from __future__ import annotations

import re
import struct
from dataclasses import dataclass

MAGIC = b"GMSG"
HEADER = struct.Struct("<4sIII")
ENTRY = struct.Struct("<H10sII")
PREFIX = 0x7F
NEW_LINE = 0x01
# Words of arguments after each control code that has any (the game's table at 0x63DCCC, where
# the plural's code 0x20 reads 4: one word, then both forms as strings in the text).
ARGUMENT_WORDS = {0x11: 1, 0x15: 1, 0x1C: 1, 0x20: 1, 0x22: 1, 0x25: 1, 0x26: 1, 0x29: 1, 0x2A: 1, 0x2B: 1,
                  0x2D: 1, 0x2F: 1, 0x31: 1, 0x32: 2, 0x3A: 1, 0x3B: 3}
_CODE = re.compile(r"\{([0-9A-Fa-f]{2})(?::([0-9A-Fa-f,]*))?\}|\n")


def to_markup(text: bytes) -> str:
    out = []
    i = 0
    while i < len(text):
        if text[i] == PREFIX:
            i += 1 + (i + 1) % 2
            code, = struct.unpack_from("<H", text, i)
            count = ARGUMENT_WORDS.get(code, 0)
            arguments = struct.unpack_from(f"<{count}H", text, i + 2)
            out.append("\n" if code == NEW_LINE else
                       f"{{{code:02X}:{','.join(f'{a:X}' for a in arguments)}}}" if count else f"{{{code:02X}}}")
            i += 2 + 2 * count
        else:
            end = text.find(bytes([PREFIX]), i)
            end = len(text) if end < 0 else end
            out.append(text[i:end].decode("utf-8"))
            i = end
    return "".join(out)


def from_markup(markup: str) -> bytes:
    out = bytearray()
    at = 0
    for match in _CODE.finditer(markup):
        out += markup[at:match.start()].encode("utf-8")
        at = match.end()
        out.append(PREFIX)
        out += bytes(len(out) % 2)
        if match.group(0) == "\n":
            out += struct.pack("<H", NEW_LINE)
            continue
        code = int(match.group(1), 16)
        arguments = [int(a, 16) for a in (match.group(2) or "").split(",") if a]
        if len(arguments) != ARGUMENT_WORDS.get(code, 0):
            raise ValueError(f"{match.group(0)}: code 0x{code:02X} takes {ARGUMENT_WORDS.get(code, 0)} words")
        out += struct.pack(f"<H{len(arguments)}H", code, *arguments)
    out += markup[at:].encode("utf-8")
    return bytes(out)


@dataclass
class Messages:
    version: int
    unknown: int
    # In the file's order: the message's number, the ten bytes after it, and its text.
    entries: list[tuple[int, bytes, bytes]]

    @classmethod
    def parse(cls, data: bytes) -> Messages:
        magic, version, count, unknown = HEADER.unpack_from(data)
        if magic != MAGIC:
            raise ValueError("not a GMSG message file")
        entries = []
        for i in range(count):
            number, rest, offset, size = ENTRY.unpack_from(data, HEADER.size + i * ENTRY.size)
            entries.append((number, rest, data[offset: offset + size]))
        return cls(version, unknown, entries)

    def build(self) -> bytes:
        table = bytearray(HEADER.pack(MAGIC, self.version, len(self.entries), self.unknown))
        texts = bytearray()
        base = HEADER.size + len(self.entries) * ENTRY.size
        for number, rest, text in self.entries:
            table += ENTRY.pack(number, rest, base + len(texts), len(text))
            texts += text
            texts += bytes(-len(texts) % 4)
        return bytes(table + texts)
