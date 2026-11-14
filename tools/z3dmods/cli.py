"""Maintainer commands for z3d-mods."""

from __future__ import annotations

import argparse
from pathlib import Path

import json

from z3dmods import azahar, config, ctr, games, harness, host_install, input_test, install, play, setup, shortcut, steps


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
        game = install.GameSet.named(cfg.repo, name)
        print(f"{name}: installed into {install.install(cfg, game)}")


def setup_game(args: argparse.Namespace) -> None:
    setup.setup(config.load(), args.rom, args.user)


def run_steps(args: argparse.Namespace) -> None:
    cfg = config.load()
    game = install.GameSet.named(cfg.repo, args.game)
    out = cfg.repo / "scratch" / "runs" / args.name
    if args.harness:
        with harness.HarnessSession(cfg, game, args.game, out / "harness.log", args.textures,
                                    args.dump_textures, args.resolution, args.option) as session:
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
    raise SystemExit(play.play(cfg, args.game))


def input_check(args: argparse.Namespace) -> None:
    print(input_test.run(config.load(), args.scenario))


def install_shortcuts(args: argparse.Namespace) -> None:
    cfg = config.load()
    paths = host_install.install(cfg, args.host) if args.host else shortcut.install(cfg)
    for path in paths:
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
    made = commands.add_parser("setup", help="install the set of a dump's game into a z3d app user folder; the app's picker runs this")
    made.add_argument("--user", type=Path, required=True, help="the app's user folder (holds Azahar/)")
    made.add_argument("rom", type=Path)
    made.set_defaults(run=setup_game)
    run = commands.add_parser("run", help="run the player's Azahar headless through scripted steps")
    run.add_argument("game", choices=["oot3d", "mm3d"])
    run.add_argument("name", help="run folder under scratch/runs/")
    run.add_argument("steps", nargs="+")
    run.add_argument("--settings", help="global Azahar settings for this run only, as JSON")
    run.add_argument("--harness", action="store_true",
                     help="run on the z3d app's harness (frame-exact, savestates) instead of the player's Azahar")
    run.add_argument("--textures", action=argparse.BooleanOptionalAction, default=None,
                     help="harness only: custom textures on or off for this run (default: the set's)")
    run.add_argument("--dump-textures", action="store_true",
                     help="harness only: every texture the game uploads, as PNG under scratch/harness/<game>/Azahar/dump/textures/")
    run.add_argument("--option", action="append", metavar="ID=VALUE",
                     help="harness only: a game option set before boot (repeatable), e.g. stairs=0")
    run.add_argument("--resolution", type=int, default=harness.RESOLUTION,
                     help="harness only: render scale, 1 is the 3DS's 400x240")
    run.set_defaults(run=run_steps)
    app = commands.add_parser("play", help="the z3d app's title picker, or one game directly, with the installed mods")
    app.add_argument("game", nargs="?", choices=["oot3d", "mm3d"])
    app.set_defaults(run=play_game)
    probe = commands.add_parser("inputtest", help="the windowed app in a headless Gamescope, driven by real key, mouse and pad events")
    probe.add_argument("scenario", choices=sorted(input_test.SCENARIOS))
    probe.set_defaults(run=input_check)
    links = commands.add_parser("shortcut", help="add the desktop shortcut that opens the app's title picker")
    links.add_argument("--host", help="install the app, the sets and its saves on this ssh host instead, in ~/Games/z3d")
    links.set_defaults(run=install_shortcuts)
    args = parser.parse_args()
    args.run(args)
