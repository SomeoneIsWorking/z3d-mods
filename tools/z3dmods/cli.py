"""Maintainer commands for z3d-mods."""

from __future__ import annotations

import argparse
from pathlib import Path

import json

from z3dmods import azahar, config, ctr, games, harness, install, play, shortcut, steps


def rominfo(args: argparse.Namespace) -> None:
    for image in args.images:
        ncch = ctr.open_cci(image)
        ex = ncch.exheader
        code = ncch.code()
        print(f"{image.name}")
        print(f"  program id   {ncch.program_id:016X}  product {ncch.product_code}")
        print(f"  exheader     title {ex.title} remaster v{ex.remaster_version} "
              f"compressed {ex.compressed_code}")
        for name, seg in (("text", ex.text), ("rodata", ex.rodata), ("data", ex.data)):
            print(f"  {name:<12} 0x{seg.address:08X} size 0x{seg.size:X} pages {seg.pages}")
        print(f"  bss          0x{ex.bss_size:X}")
        print(f"  code.bin     size 0x{len(code):X} sha256 {ctr.sha256(code)}")
        print(f"  exheader     sha256 {ctr.sha256(ex.raw[:0x400])}")


def install_set(args: argparse.Namespace) -> None:
    cfg = config.load()
    for name in args.games:
        game = install.GameSet.load(cfg.repo / "sets" / f"{name}.json")
        print(f"{name}: installed into {install.install(cfg, game)}")


def run_steps(args: argparse.Namespace) -> None:
    cfg = config.load()
    game = install.GameSet.load(cfg.repo / "sets" / f"{args.game}.json")
    out = cfg.repo / "scratch" / "runs" / args.name
    if args.harness:
        with harness.HarnessSession(cfg, game, args.game, out / "harness.log",
                                    dict(option.split("=", 1) for option in args.core_option)) as session:
            steps.run(session, args.steps, out, games.layout(cfg.repo, args.game))
            print(f"{session.frames()} frames")
        return
    settings = json.loads(args.settings) if args.settings else None
    with azahar.Session(cfg, cfg.rom(game.title_id), out / "gamescope.log", settings) as session:
        steps.run(session, args.steps, out, games.layout(cfg.repo, args.game))
    for name, lines in session.config_changes.items():
        print(f"restored {name}: Azahar changed {len(lines)} lines")


def play_game(args: argparse.Namespace) -> None:
    cfg = config.load()
    game = install.GameSet.load(cfg.repo / "sets" / f"{args.game}.json")
    raise SystemExit(play.play(cfg, args.game, game))


def install_shortcuts(_: argparse.Namespace) -> None:
    for path in shortcut.install(config.load()):
        print(path)


def main() -> None:
    parser = argparse.ArgumentParser(prog="z3dmods")
    commands = parser.add_subparsers(required=True)
    info = commands.add_parser("rominfo", help="print title, revision and code hashes of a .3ds")
    info.add_argument("images", nargs="+", type=Path)
    info.set_defaults(run=rominfo)
    inst = commands.add_parser("install", help="install a game's mod set into Azahar")
    inst.add_argument("games", nargs="+", choices=["oot3d", "mm3d"])
    inst.set_defaults(run=install_set)
    run = commands.add_parser("run", help="run the player's Azahar headless through scripted steps")
    run.add_argument("game", choices=["oot3d", "mm3d"])
    run.add_argument("name", help="run folder under scratch/runs/")
    run.add_argument("steps", nargs="+")
    run.add_argument("--settings", help="global Azahar settings for this run only, as JSON")
    run.add_argument("--harness", action="store_true",
                     help="run on the libretro harness (frame-exact, savestates) instead of the player's Azahar")
    run.add_argument("--core-option", action="append", default=[], metavar="KEY=VALUE",
                     help="harness only: a libretro core option for this run, e.g. citra_custom_textures=disabled")
    run.set_defaults(run=run_steps)
    app = commands.add_parser("play", help="play a game in the z3d app with its installed mods")
    app.add_argument("game", choices=["oot3d", "mm3d"])
    app.set_defaults(run=play_game)
    links = commands.add_parser("shortcut", help="add desktop shortcuts that play each game in the app")
    links.set_defaults(run=install_shortcuts)
    args = parser.parse_args()
    args.run(args)
