"""QM, OoT3D's message file (decomp/oot3d docs/message.md, "The message file"): a table of
messages, each with its text in up to nine languages, and the text as markup.

Markup is the text with each control code as `{XX}` or `{XX:<argument bytes>}` in hex and a
line break (code 0x1C) as a newline, so an edit table reads like the message."""

from __future__ import annotations

import re
import struct
from dataclasses import dataclass

MAGIC = b"QM\0\0"
HEADER = struct.Struct("<4sIII")
ENTRY = struct.Struct("<24I")
LANGUAGES = 9
PREFIX = 0x7F
NEW_LINE = 0x1C
# Bytes of arguments after each control code that has any (the game's table at 0x4D6708).
ARGUMENT_BYTES = {0x02: 1, 0x03: 2, 0x06: 1, 0x08: 1, 0x0A: 1, 0x0D: 5, 0x0E: 5, 0x0F: 1, 0x10: 1, 0x11: 3,
                  0x18: 1, 0x1A: 4, 0x1B: 6, 0x1D: 1, 0x23: 1, 0x24: 1, 0x25: 3, 0x26: 1}
_CODE = re.compile(r"\{([0-9A-Fa-f]{2})(?::([0-9A-Fa-f]*))?\}|\n")


def to_markup(text: bytes) -> str:
    out = []
    i = 0
    while i < len(text):
        if text[i] == PREFIX:
            code = text[i + 1]
            size = ARGUMENT_BYTES.get(code, 0)
            arguments = text[i + 2: i + 2 + size]
            out.append("\n" if code == NEW_LINE else f"{{{code:02X}:{arguments.hex()}}}" if size else f"{{{code:02X}}}")
            i += 2 + size
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
        if match.group(0) == "\n":
            out += bytes([PREFIX, NEW_LINE])
            continue
        code, arguments = int(match.group(1), 16), bytes.fromhex(match.group(2) or "")
        if len(arguments) != ARGUMENT_BYTES.get(code, 0):
            raise ValueError(f"{match.group(0)}: code 0x{code:02X} takes {ARGUMENT_BYTES.get(code, 0)} bytes")
        out += bytes([PREFIX, code]) + arguments
    out += markup[at:].encode("utf-8")
    return bytes(out)


@dataclass
class Messages:
    version: int
    # In the file's order: the message's number, its five header words, and its text by language.
    entries: list[tuple[int, tuple[int, ...], dict[int, bytes]]]

    @classmethod
    def parse(cls, data: bytes) -> Messages:
        magic, version, count, _ = HEADER.unpack_from(data)
        if magic != MAGIC:
            raise ValueError("not a QM message file")
        entries = []
        for i in range(count):
            words = ENTRY.unpack_from(data, HEADER.size + i * ENTRY.size)
            texts = {language: data[words[6 + language * 2]: words[6 + language * 2] + words[7 + language * 2]]
                     for language in range(LANGUAGES) if words[7 + language * 2]}
            entries.append((words[0], words[1:6], texts))
        return cls(version, entries)

    def build(self) -> bytes:
        table = bytearray(HEADER.pack(MAGIC, self.version, len(self.entries), 0))
        texts = bytearray()
        base = HEADER.size + len(self.entries) * ENTRY.size
        for number, words, by_language in self.entries:
            places = []
            for language in range(LANGUAGES):
                text = by_language.get(language, b"")
                places += [base + len(texts) if text else 0, len(text)]
                texts += text
                texts += bytes(-len(texts) % 4)
            table += ENTRY.pack(number, *words, *places)
        return bytes(table + texts)
