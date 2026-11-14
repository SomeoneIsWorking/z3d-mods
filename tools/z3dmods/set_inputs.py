"""What of ours a game's set is made from, as one digest: the set and its tables, the patch
(its source in a checkout, its build in a package), the drawn glyphs, these tools and the app's
key bindings, which the text's key caps are drawn from (app_keys.py). The
install writes it into the set's options manifest and a package's launcher tells the app the
one it would write now, so a set made by an older package is made again.
"""

from __future__ import annotations

import hashlib
from pathlib import Path

from z3dmods import config, glyphs, source_check

# The manifest's and the library's key for the digest (app/src/set_install.cpp kInputs).
KEY = "inputs"


def digest(repo: Path, game: str, keys: str) -> str:
    """`game` names the set (sets/<game>.json) and its patch; `keys` is the app's `--keys` listing."""
    parts = (repo / "sets", repo / "patch" / game, repo / "patch" / "common", repo / config.PREBUILT / game,
             repo / config.PREBUILT / glyphs.FOLDER, Path(__file__).parent)
    lines = []
    for part in parts:
        files = sorted(p for p in part.rglob("*") if p.is_file() and "__pycache__" not in p.parts)
        lines += [f"{source_check.digest(p)}  {p.relative_to(repo).as_posix()}\n" for p in files]
    return hashlib.sha256(("".join(lines) + keys).encode()).hexdigest()
