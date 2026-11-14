"""Guest addresses and offsets from each game's decomp (decomp/<game>, a submodule).

Reads the decomp's function maps (linker-script lines `name 0xaddr` or a `vaddr,name` CSV) and
headers (`/* 0x.. */` field offset comments, `} Name; // size = 0x..`, `extern T name; //0xaddr`
globals, `#define` integer expressions, `/* 0x.. */ NAME,` enumerators). A patch names what it needs in patch/<game>/symbols.toml
and gets a generated C++ header and linker-script fragment; the tools resolve names directly.
"""

from __future__ import annotations

import ast
import csv
import json
import operator
import re
import subprocess
import tomllib
from dataclasses import dataclass, field
from pathlib import Path

_LD_LINE = re.compile(r"^\s+([A-Za-z_]\w*) 0x([0-9A-Fa-f]+)\s*$", re.M)
_STRUCT_OPEN = re.compile(r"^\s*(?:typedef\s+)?struct\s*(\w*)\s*\{")
_STRUCT_CLOSE = re.compile(r"^\s*\}\s*(\w*)\s*;")
_FIELD = re.compile(r"/\*\s*0x([0-9A-Fa-f]+)\s*\*/[^;]*?\b(\w+)\s*(?:\[[^\]]*\])*\s*;")
_EXTERN = re.compile(r"^\s*extern\s+[^;]*?\b(\w+)\s*(?:\[[^\]]*\])?\s*;\s*//\s*0x([0-9A-Fa-f]+)")
_DEFINE = re.compile(r"^\s*#define\s+(\w+)\s+([^/\n]+?)\s*(?://.*)?$")
_ENUMERATOR = re.compile(r"^\s*/\*\s*0x([0-9A-Fa-f]+)\s*\*/\s*([A-Z][A-Z0-9_]*)\s*(?:=[^,]*)?,?\s*(?://.*)?$")
_SIZE = re.compile(r"//\s*size\s*=\s*0x([0-9A-Fa-f]+)")
_OPERATORS = {ast.Add: operator.add, ast.Sub: operator.sub, ast.Mult: operator.mul,
              ast.LShift: operator.lshift, ast.RShift: operator.rshift, ast.BitOr: operator.or_}


@dataclass(frozen=True)
class Source:
    path: str
    function_maps: tuple[str, ...]
    headers: tuple[str, ...]


SOURCES = {
    "oot3d": Source("decomp/oot3d", ("data/oot3d.ld",),
                    ("include/z3D.hpp", "include/z3Dactor.hpp", "include/z3Dcamera.hpp", "include/z3Dsave.hpp",
                     "include/variables.hpp", "include/regs.hpp", "include/z3Dcutscene.hpp",
                     "include/z3Dmessage.hpp", "include/z3Ditem.hpp", "include/z3Dbgcheck.hpp")),
    "mm3d": Source("decomp/mm3d", ("symbols/functions.csv",), ("include/z3D.h",)),
}


@dataclass
class Decomp:
    functions: dict[str, int] = field(default_factory=dict)
    globals: dict[str, int] = field(default_factory=dict)
    # "Struct.field" -> byte offset
    offsets: dict[str, int] = field(default_factory=dict)
    sizes: dict[str, int] = field(default_factory=dict)
    defines: dict[str, str] = field(default_factory=dict)

    def define(self, name: str) -> int:
        def value(node: ast.AST) -> int:
            if isinstance(node, ast.Constant) and isinstance(node.value, int):
                return node.value
            if isinstance(node, ast.Name) and node.id in self.defines:
                return self.define(node.id)
            if isinstance(node, ast.UnaryOp) and isinstance(node.op, ast.USub):
                return -value(node.operand)
            if isinstance(node, ast.BinOp) and type(node.op) in _OPERATORS:
                return _OPERATORS[type(node.op)](value(node.left), value(node.right))
            raise ValueError(f"#define {name}: not an integer expression")
        return value(ast.parse(self.defines[name], mode="eval").body)


def _read_functions(path: Path, into: dict[str, int]) -> None:
    if path.suffix == ".csv":
        with path.open(newline="") as rows:
            for row in csv.DictReader(rows):
                into[row["name"]] = int(row["vaddr"], 16)
        return
    for name, address in _LD_LINE.findall(path.read_text()):
        into[name] = int(address, 16)


def _read_header(path: Path, decomp: Decomp) -> None:
    depth = 0
    struct_fields: dict[str, int] = {}
    tag = ""
    for line in path.read_text(errors="replace").splitlines():
        code = re.sub(r"//.*|/\*.*?\*/", "", line)
        if depth == 0:
            if (opened := _STRUCT_OPEN.match(line)) is not None:
                tag, struct_fields = opened.group(1), {}
            elif (extern := _EXTERN.match(line)) is not None:
                decomp.globals[extern.group(1)] = int(extern.group(2), 16)
                continue
            elif (define := _DEFINE.match(line)) is not None:
                decomp.defines[define.group(1)] = define.group(2)
                continue
            elif (enumerator := _ENUMERATOR.match(line)) is not None:
                decomp.defines[enumerator.group(2)] = f"0x{enumerator.group(1)}"
                continue
            else:
                continue
        elif depth == 1:
            if (member := _FIELD.search(line)) is not None:
                struct_fields[member.group(2)] = int(member.group(1), 16)
        depth += code.count("{") - code.count("}")
        if depth == 0 and (closed := _STRUCT_CLOSE.match(line)) is not None:
            size = _SIZE.search(line)
            for name in {closed.group(1) or tag, tag} - {""}:
                for member, offset in struct_fields.items():
                    decomp.offsets[f"{name}.{member}"] = offset
                if size is not None:
                    decomp.sizes[name] = int(size.group(1), 16)


# A package's copy of what the tools read from a decomp: everything but the function map.
PACKAGED = "symbols.json"


def save(decomp: Decomp, path: Path) -> None:
    table = {"globals": decomp.globals, "offsets": decomp.offsets, "sizes": decomp.sizes, "defines": decomp.defines}
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(table, indent=1) + "\n")


def load(repo: Path, game: str) -> Decomp:
    source = SOURCES[game]
    root = repo / source.path
    packaged = repo / "prebuilt" / game / PACKAGED
    if not root.exists() and packaged.exists():
        return Decomp(**json.loads(packaged.read_text()))
    if not root.is_dir() or not any(root.iterdir()):
        raise RuntimeError(f"{root} is empty: git submodule update --init {source.path}")
    decomp = Decomp()
    for path in source.function_maps:
        _read_functions(root / path, decomp.functions)
    for path in source.headers:
        _read_header(root / path, decomp)
    return decomp


def _lookup(table: dict[str, int], kind: str, name: str) -> int:
    if name not in table:
        raise KeyError(f"no {kind} {name} in the decomp")
    return table[name]


def function(decomp: Decomp, name: str) -> int:
    return _lookup(decomp.functions, "function", name)


def global_(decomp: Decomp, name: str) -> int:
    return _lookup(decomp.globals, "global", name)


def offset(decomp: Decomp, name: str) -> int:
    return _lookup(decomp.offsets, "field", name)


def size(decomp: Decomp, name: str) -> int:
    return _lookup(decomp.sizes, "struct size", name)


@dataclass(frozen=True)
class Generated:
    header: Path
    linker: Path


def generate(repo: Path, game: str, out: Path) -> Generated:
    manifest = tomllib.loads((repo / "patch" / game / "symbols.toml").read_text())
    decomp = load(repo, game)
    addresses = {ours: function(decomp, theirs) for ours, theirs in manifest.get("functions", {}).items()}
    addresses |= {ours: global_(decomp, theirs) for ours, theirs in manifest.get("globals", {}).items()}
    offsets = {ours: offset(decomp, theirs) for ours, theirs in manifest.get("offsets", {}).items()}
    offsets |= {ours: size(decomp, theirs) for ours, theirs in manifest.get("sizes", {}).items()}
    values = {ours: decomp.define(theirs) for ours, theirs in manifest.get("defines", {}).items()}

    root = repo / SOURCES[game].path
    revision = subprocess.run(["git", "-C", str(root), "rev-parse", "--short=12", "HEAD"],
                              capture_output=True, text=True, check=True).stdout.strip()
    origin = f"{SOURCES[game].path} {revision}"
    lines = [f"// Generated by tools/z3dmods/symbols.py from {origin}; do not edit.", "#pragma once", "",
             "#include <stdint.h>", "", f"namespace z3dmods::{game} {{", ""]
    lines += [f"inline constexpr uintptr_t {k} = 0x{v:08X};" for k, v in addresses.items()]
    lines += [f"inline constexpr uint32_t {k} = 0x{v:X};" for k, v in offsets.items()]
    lines += [f"inline constexpr int32_t {k} = {v};" for k, v in values.items()]
    lines += ["", f"}} // namespace z3dmods::{game}", ""]
    out.mkdir(parents=True, exist_ok=True)
    header = out / "symbols.hpp"
    header.write_text("\n".join(lines))
    linker = out / "symbols.ld"
    linker.write_text(f"/* Generated from {origin}. */\n" +
                      "".join(f"{k} = 0x{v:08X};\n" for k, v in addresses.items()))
    return Generated(header, linker)
