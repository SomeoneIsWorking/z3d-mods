"""A set's third-party files found among the player's own downloads by the hashes the set names.

A file's digest is its SHA-256. A folder's is the SHA-256 of its `sha256sum` listing: one
"<file digest>  <path>" line per file, paths relative to the folder, sorted.

The player keeps their downloads in one folder, each unpacked wherever they like inside it; a
`.zip` or `.tar` archive left there is unpacked beside itself first. A source is the copy whose
digest is the set's, wherever it sits; one that is missing or different is refused with its
name and where to get it.
"""

from __future__ import annotations

import hashlib
import shutil
import tarfile
import zipfile
from pathlib import Path

CHUNK = 1 << 20
# Archive formats this reads; others are named for the player to unpack.
UNPACKED = (".zip", ".tar", ".tar.gz", ".tgz", ".tar.xz", ".tar.bz2")
OTHER_ARCHIVES = (".7z", ".rar")
PARTIAL = ".partial"


class Refused(RuntimeError):
    """The player's files are not what a set needs; the message is written for them."""


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


def _suffix(path: Path, suffixes: tuple[str, ...]) -> str | None:
    return next((s for s in suffixes if path.name.lower().endswith(s)), None)


def unpack(folder: Path) -> None:
    """Unpacks every archive in `folder` that has no unpacked folder beside it yet."""
    for archive in sorted(p for p in folder.rglob("*") if p.is_file() and _suffix(p, UNPACKED)):
        target = archive.with_name(archive.name[:-len(_suffix(archive, UNPACKED))])
        if target.exists():
            continue
        print(f"Unpacking {archive.name}")
        partial = target.with_name(target.name + PARTIAL)
        if partial.exists():
            shutil.rmtree(partial)
        if zipfile.is_zipfile(archive):
            with zipfile.ZipFile(archive) as opened:
                opened.extractall(partial)
        else:
            with tarfile.open(archive) as opened:
                opened.extractall(partial, filter="data")
        partial.rename(target)


def _unread_archives(folder: Path) -> list[str]:
    return sorted(p.name for p in folder.rglob("*") if p.is_file() and _suffix(p, OTHER_ARCHIVES))


def _advice(folder: Path, name: str, origin: str) -> list[str]:
    lines = [f"Get it from {origin}", f"Unpack it into {folder}"]
    if archives := _unread_archives(folder):
        lines.append(f"This setup cannot open .7z or .rar: unpack {', '.join(archives)} there yourself")
    return lines


def find_files(folder: Path, name: str, origin: str, anchor: str, expected: dict[str, str]) -> Path:
    """The folder under `folder` whose entries `expected` (relative path -> digest) are the
    set's. `anchor` is the entry that is a file: every file of its name is tried as that entry."""
    depth = len(Path(anchor).parts)
    roots = [p.parents[depth - 1] for p in sorted(folder.rglob(Path(anchor).name)) if p.is_file()] if folder.is_dir() else []
    roots = [root for root in roots if (root / anchor).is_file()]
    best: dict[str, str] | None = None
    for root in roots:
        states = {}
        for entry, wanted in expected.items():
            path = root / entry
            states[entry] = "missing" if not path.exists() else "found" if digest(path) == wanted else "different"
        if all(state == "found" for state in states.values()):
            return root
        if best is None or sum(s == "found" for s in states.values()) > sum(s == "found" for s in best.values()):
            best = states
    if best is None:
        raise Refused("\n".join([f"{name} is missing from your mods folder", *_advice(folder, name, origin)]))
    different = [entry for entry, state in best.items() if state == "different"]
    missing = [entry for entry, state in best.items() if state == "missing"]
    lines = [f"{name} is not the version this setup was made for"]
    if different:
        lines.append(f"Different: {', '.join(different)}")
    if missing:
        lines.append(f"Missing: {', '.join(missing)}")
    raise Refused("\n".join(lines + _advice(folder, name, origin)))


def find_folder(folder: Path, name: str, origin: str, named: str, expected: str) -> Path:
    """The folder called `named` under `folder` whose digest is the set's."""
    found = [p for p in sorted(folder.rglob(named)) if p.is_dir()] if folder.is_dir() else []
    for candidate in found:
        if digest(candidate) == expected:
            return candidate
    if not found:
        raise Refused("\n".join([f"{name} is missing from your mods folder", *_advice(folder, name, origin)]))
    raise Refused("\n".join([f"{name} is not the version this setup was made for",
                             f"Different: {', '.join(str(p.relative_to(folder)) for p in found)}",
                             *_advice(folder, name, origin)]))
