"""Install a game's mod set into Azahar's load/ folders, recording every file it writes."""

from __future__ import annotations

import hashlib
import json
import shutil
import struct
from dataclasses import dataclass
from pathlib import Path

from z3dmods import azahar_config, bgcheck, bps, ctr, games, hud, hud_icons, ips, lzs, options, patch_build, source_check, stairs, symbols, zsi
from z3dmods.config import Config
from z3dmods.texture_pack import TexturePack

RECORD = "z3dmods-install.json"
PAGE = 0x1000
EXHEADER_BSS_SIZE = 0x3C
HUD_FOLDER = "z3dmods-hud"
# The layer switch in the patch's options.toml that turns the stepped stairs on.
STAIRS_OPTION = "stairs"


@dataclass(frozen=True)
class GameSet:
    title_id: str
    # The dump the set is made for, in the player's words.
    dump: str
    code_sha256: str
    base: dict
    textures: dict | None
    azahar_global: dict
    patch: str | None
    # Where the patch's payload starts: the page after the base mod's .bss.
    payload: int | None
    # The screen layout the set is made for: "single_screen" when the mod draws everything on top.
    screen_layout: str
    # Fold the ramps drawn with a texture whose name matches this into steps, in the folder of
    # the patch's "stairs" layer switch.
    stairs: str | None
    # The menu-texture edit table (sets/hud/): the base mod's edits redone on the texture pack's
    # copies, and the pad glyphs drawn over the game's button prompts.
    hud: str | None


    @classmethod
    def named(cls, repo: Path, name: str) -> GameSet:
        """A game's set, sets/<name>.json."""
        raw = json.loads((repo / "sets" / f"{name}.json").read_text())
        return cls(raw["title_id"], raw["dump"], raw["code_sha256"], raw["base"], raw.get("textures"),
                   raw.get("azahar_global", {}), raw.get("patch"),
                   int(raw["payload"], 16) if "payload" in raw else None, raw.get("screen_layout", "default"),
                   raw.get("stairs"), raw.get("hud"))


def _sources(cfg: Config, game: GameSet) -> tuple[Path, Path | None]:
    """The base mod's folder and the texture pack's among the player's downloads: the copies
    the set names by hash (source_check.py)."""
    used = {game.base[key] for key in ("code_patch", "exheader", "romfs") if key in game.base}
    if used != set(game.base["sha256"]):
        raise RuntimeError(f"{game.title_id}: the set's base hashes do not cover exactly {sorted(used)}")
    source_check.unpack(cfg.mod_sources)
    base = source_check.find_files(cfg.mod_sources, game.base["name"], game.base["from"],
                                   game.base["code_patch"], game.base["sha256"])
    textures = game.textures and source_check.find_folder(
        cfg.mod_sources, game.textures["name"], game.textures["from"], game.textures["folder"],
        game.textures["sha256"])
    return base, textures


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


def _copy_tree(source: Path, dest: Path, replaced: frozenset[str] = frozenset()) -> None:
    """Copies a folder; files named in `replaced` stay out."""
    for path in source.rglob("*"):
        if path.is_file() and path.name not in replaced:
            target = dest / path.relative_to(source)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, target)


def _page(value: int) -> int:
    return (value + PAGE - 1) & ~(PAGE - 1)


def _apply(patch_name: str, patch: bytes, code: bytes) -> bytes:
    return bps.apply(patch, code) if patch_name.endswith(".bps") else ips.apply(patch, code)


def _layer(cfg: Config, game: GameSet, code: bytes, base_exheader: bytes, mods: Path, layers: set[str]) -> None:
    """Write the base mod's code as code.bin and put our patch on top as code.ips."""
    exheader = ctr.Exheader.parse(base_exheader)
    data_end = exheader.data.address + exheader.data.pages * PAGE
    payload = data_end + _page(exheader.bss_size)
    if payload != game.payload:
        raise RuntimeError(f"{game.title_id}: the base mod leaves 0x{payload:X} for our patch, the set says "
                           f"0x{game.payload or 0:X}")
    ours = patch_build.patch(cfg.repo, game.patch, payload)
    for offset, original in ours.expected.items():
        if code[offset:offset + len(original)] != original:
            raise RuntimeError(f"0x{offset + patch_build.CODE_BASE:08X} in the base mod's code is not the "
                               f"instruction our hook replaces")
    grown = bytearray(base_exheader)
    struct.pack_into("<I", grown, EXHEADER_BSS_SIZE, _page(ours.payload_end - data_end))
    (mods / "code.bin").write_bytes(code)
    (mods / "code.ips").write_bytes(ips.build(ours.records))
    (mods / "exheader.bin").write_bytes(bytes(grown))
    (mods / options.MANIFEST).write_text(options.manifest(cfg.repo, game.patch, ours.payload_start, layers))


def _stepped_rooms(rom: ctr.Ncch, base_romfs: Path | None, named: str, pack: TexturePack | None,
                   rules: bgcheck.Rules) -> tuple[dict[str, bytes], dict[str, bgcheck.Use]]:
    """Every room with stair ramps, stepped, and its scene with the collision on those ramps
    stepped; a file the base mod replaces is stepped from its copy. With them, what each
    stepped scene's collision takes of the game's room for it; a scene over that is refused."""
    files = rom.romfs()

    def read(path: str) -> bytes:
        base = base_romfs / path if base_romfs else None
        return lzs.unpacked(base.read_bytes() if base and base.exists() else files.read(path))

    out = {}
    drawn: dict[str, list[stairs.Ramp]] = {}
    for path in files.files:
        scene = stairs.scene_of(path)
        if scene is None:
            continue
        data, stepped = stairs.room(read(path), named, pack)
        if stepped:
            out[path] = data
            drawn.setdefault(scene, []).extend(stairs.room_ramps(read(path), named, pack))
    ids = rules.scene_ids()
    uses = {}
    for scene, ramps in drawn.items():
        data = stairs.collision(zsi.Scene.parse(read(scene)), ramps)
        if data is None:
            continue
        if scene not in ids:
            raise RuntimeError(f"{scene}: not in the game's scene table")
        uses[scene] = rules.use(zsi.Scene.parse(data), ids[scene])
        if (over := uses[scene].over()) is not None:
            raise RuntimeError(f"{scene}: stepped, it has {over}")
        out[scene] = data
    return out, uses


def install(cfg: Config, game: GameSet) -> Path:
    rom_path = cfg.rom(game.title_id)
    try:
        rom = ctr.open_cci(rom_path)
        vanilla = rom.code()
    except ValueError as error:
        raise source_check.Refused(f"{rom_path.name} is not a decrypted 3DS dump\n{error}") from error
    if ctr.sha256(vanilla) != game.code_sha256:
        raise source_check.Refused(f"{rom_path.name} is not the dump this setup was made for\n"
                                   f"It needs {game.dump}")
    root, texture_root = _sources(cfg, game)
    code_patch = root / game.base["code_patch"]

    base_romfs = root / game.base["romfs"] if "romfs" in game.base else None
    pack = TexturePack(texture_root) if texture_root else None
    if game.hud and not pack:
        raise RuntimeError(f"{game.title_id}: the menu textures are built on a texture pack, and the set has none")
    if game.stairs and not game.patch:
        raise RuntimeError(f"{game.title_id}: stairs want the patch whose options.toml switches them")
    stairs_layer = options.layer(cfg.repo, game.patch, STAIRS_OPTION) if game.stairs else None
    code = _apply(game.base["code_patch"], code_patch.read_bytes(), vanilla) if game.patch else vanilla
    rooms, uses = (_stepped_rooms(rom, base_romfs, game.stairs, pack,
                                  bgcheck.Rules(game.patch, symbols.load(cfg.repo, game.patch), code))
                   if game.stairs else ({}, {}))
    for scene, use in uses.items():
        print(f"{scene}: {use}")
    menus = (hud.build(rom.romfs(), base_romfs, pack, cfg.repo / "sets" / game.hud, cfg.key_font(),
                      cfg.glyph_set())
             if game.hud else {})

    mods = cfg.mods_dir(game.title_id)
    _remove_recorded(mods)
    mods.mkdir(parents=True, exist_ok=True)
    if game.patch:
        _layer(cfg, game, code, (root / game.base["exheader"]).read_bytes(),
               mods, {stairs_layer} if stairs_layer else set())
    else:
        shutil.copyfile(code_patch, mods / Path(game.base["code_patch"]).name)
        shutil.copyfile(root / game.base["exheader"], mods / "exheader.bin")
    if base_romfs:
        _copy_tree(base_romfs, mods / "romfs")
    for item, png in (hud_icons.build(cfg.repo, game.patch, rom.romfs(), pack, code) if game.patch else {}).items():
        target = mods / games.HUD_ICONS / f"{item}.png"
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(png)
    for path, data in rooms.items():
        target = mods / stairs_layer / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    _write_record(mods, {"base": game.base["name"], "patch": game.patch or "",
                         "stairs": "yes" if game.stairs else "no"})

    if game.textures or menus:
        textures = cfg.textures_dir(game.title_id)
        _remove_recorded(textures)
        textures.mkdir(parents=True, exist_ok=True)
        if texture_root:
            # Azahar keeps one texture per hash: ours stand in for the pack's copies of the same name.
            _copy_tree(texture_root, textures, frozenset(menus))
        for name, png in menus.items():
            target = textures / HUD_FOLDER / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(png)
        _write_record(textures, {"textures": game.textures["name"] if game.textures else "",
                                 "hud": game.hud or ""})
    if game.azahar_global:
        azahar_config.set_global(cfg.azahar_config, game.azahar_global)
    return mods
