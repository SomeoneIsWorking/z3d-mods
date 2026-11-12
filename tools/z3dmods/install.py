"""Install a game's mod set into Azahar's load/ folders, recording every file it writes."""

from __future__ import annotations

import hashlib
import json
import shutil
from dataclasses import dataclass
from pathlib import Path

from z3dmods import azahar_config, bps, ctr, ips
from z3dmods.config import Config

RECORD = "z3dmods-install.json"


@dataclass(frozen=True)
class GameSet:
    title_id: str
    code_sha256: str
    base: dict
    textures: dict | None
    azahar_global: dict


    @classmethod
    def load(cls, path: Path) -> GameSet:
        raw = json.loads(path.read_text())
        return cls(raw["title_id"], raw["code_sha256"], raw["base"], raw.get("textures"),
                   raw.get("azahar_global", {}))


def _sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _remove_recorded(folder: Path) -> None:
    record = folder / RECORD
    if not record.exists():
        if folder.exists() and any(folder.iterdir()):
            raise RuntimeError(f"{folder} has files z3dmods did not install; move them first")
        return
    for rel, digest in json.loads(record.read_text())["files"].items():
        path = folder / rel
        if path.exists() and _sha(path) != digest:
            raise RuntimeError(f"{path} changed since install; not removing it")
        path.unlink(missing_ok=True)
    record.unlink()
    for directory in sorted((p for p in folder.rglob("*") if p.is_dir()), reverse=True):
        if not any(directory.iterdir()):
            directory.rmdir()


def _write_record(folder: Path, sources: dict[str, str]) -> None:
    files = {str(p.relative_to(folder)): _sha(p) for p in folder.rglob("*") if p.is_file()}
    (folder / RECORD).write_text(json.dumps({"sources": sources, "files": files}, indent=2) + "\n")


def _copy_tree(source: Path, dest: Path) -> None:
    for path in source.rglob("*"):
        if path.is_file():
            target = dest / path.relative_to(source)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, target)


def install(cfg: Config, game: GameSet) -> Path:
    root = cfg.env_assets / game.base["root"]
    code_patch = root / game.base["code_patch"]
    ctr_code = ctr.open_cci(cfg.rom(game.title_id)).code()
    if ctr.sha256(ctr_code) != game.code_sha256:
        raise RuntimeError(f"{game.title_id}: ROM code.bin is not the revision this set targets")

    mods = cfg.mods_dir(game.title_id)
    _remove_recorded(mods)
    mods.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(code_patch, mods / Path(game.base["code_patch"]).name)
    shutil.copyfile(root / game.base["exheader"], mods / "exheader.bin")
    if "romfs" in game.base:
        _copy_tree(root / game.base["romfs"], mods / "romfs")
    _write_record(mods, {"base": game.base["name"]})

    if game.textures:
        textures = cfg.textures_dir(game.title_id)
        _remove_recorded(textures)
        textures.mkdir(parents=True, exist_ok=True)
        _copy_tree(cfg.env_assets / game.textures["root"], textures)
        _write_record(textures, {"textures": game.textures["name"]})
    if game.azahar_global:
        azahar_config.set_global(cfg.azahar_config, game.azahar_global)
    return mods
