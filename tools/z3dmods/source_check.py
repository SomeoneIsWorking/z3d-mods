"""A set's third-party files checked against the hashes the set names for them.

A file's digest is its SHA-256. A folder's is the SHA-256 of its `sha256sum` listing: one
"<file digest>  <path>" line per file, paths relative to the folder, sorted.
"""

from __future__ import annotations

import hashlib
from pathlib import Path

CHUNK = 1 << 20


def _file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(CHUNK):
            digest.update(chunk)
    return digest.hexdigest()


def digest(path: Path) -> str:
    if path.is_file():
        return _file(path)
    files = sorted((p.relative_to(path).as_posix(), p) for p in path.rglob("*") if p.is_file())
    listing = "".join(f"{_file(p)}  {name}\n" for name, p in files)
    return hashlib.sha256(listing.encode()).hexdigest()


def check(path: Path, expected: str) -> None:
    """Refuses a file or folder that is not the one the set was made for."""
    if not path.exists():
        raise RuntimeError(f"{path} is missing; the set needs the player's own copy of it")
    found = digest(path)
    if found != expected:
        raise RuntimeError(f"{path} is not the copy this set was made for (sha256 {found}, set has {expected})")
