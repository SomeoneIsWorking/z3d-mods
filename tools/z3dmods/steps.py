"""Run a scripted session.

Steps: wait:<s>, shot:<name>, <button>, hold:<button>,<s>, peek:<addr>,<size>,
poke:<addr>,<hex bytes> (<addr> may be play+<offset>, from the play state, or player+<offset>, from the player actor), warp:<entrance>, teleport:<x>,<y>,<z> (the player), where (print the player's position),
walk:<x>,<z>[,<s>] (harness: the circle pad steered until the player stands at x, z, printing the way), waitcs:<s>[,<button>] (wait for a cutscene to start, then end,
pressing <button> meanwhile), csframe:<n> (wait until a running cutscene
reaches frame n), save:<name> (dump the save context),
cs (print the cutscene context and script header),
arena:<addr>[,<name>] (walk a game arena's blocks: used, free and the largest free block; with a
name, the block list goes to <name>.arena.txt).
Harness only: savestate:<name>, loadstate:<name>, watch:<addr>,<size>, hits, keydown:<button>,
keyup:<button> (hold across other steps), stick:<x>,<y>,<s> (circle pad, -32767..32767, y down),
cstick:<x>,<y>,<s> (the New 3DS C-stick, the same way),
gyro:<x>,<y>,<z>,<s> (the 3DS turning, degrees a second on the core's axes),
mouse:<dx>,<dy> (the mouse moved by that many counts while the look follows the gyroscope: the aim),
device:<pad|keyboard> (which device the input stands for: the HUD's prompts follow it),
record:<name> (every frame from here into <name>.mp4), stoprecord,
option:<id>,<value> (a game option, docs/app.md "Options"),
touch:<x>,<y>[,<s>] (the stylus on the 320x240 touch screen),
patch:<on|off> and textures:<on|off> (the running game's mods, docs/app.md "Hotswap").
"""

from __future__ import annotations

import math
import struct
import time
from pathlib import Path

from typing import Protocol

from z3dmods.games import TRANSITION_START, Layout


class Session(Protocol):
    """What a run needs from a backend: the standalone Azahar or the app's harness."""

    def read(self, address: int, size: int) -> bytes: ...
    def u32(self, address: int) -> int: ...
    def write(self, address: int, data: bytes) -> None: ...
    def screenshot(self, path: Path) -> tuple[int, int]: ...
    def wait(self, seconds: float) -> None: ...
    def hold(self, button: str, seconds: float) -> None: ...
    def press(self, button: str) -> None: ...


def _play(session: Session, layout: Layout) -> int:
    return session.u32(layout.play_pointer)


def _player(session: Session, layout: Layout) -> int:
    if layout.player is None:
        raise RuntimeError("no player actor layout for this game")
    return session.u32(_play(session, layout) + layout.player)


def _address(session: Session, layout: Layout, text: str) -> int:
    """An absolute address, play+<offset> from the current play state or player+<offset> from the player actor."""
    if text.startswith("play+"):
        return _play(session, layout) + int(text[5:], 0)
    if text.startswith("player+"):
        return _player(session, layout) + int(text[7:], 0)
    return int(text, 0)


def _cutscene(session: Session, layout: Layout) -> tuple[int, int]:
    """(state, frame) of the cutscene context; (0, 0) while no play state exists."""
    play = _play(session, layout)
    if play == 0:
        return 0, 0
    state = session.read(play + layout.cutscene_state, 1)[0]
    frames = struct.unpack("<H", session.read(play + layout.cutscene_frames, 2))[0]
    return state, frames


POLL = 0.1


def _wait_cutscene(session: Session, layout: Layout, timeout: float, button: str, log) -> None:
    seen = False
    last = (0, 0)
    for _ in range(round(timeout / POLL)):
        state, frames = _cutscene(session, layout)
        if state and not seen:
            log(f"cutscene running (state {state})")
            seen = True
        if seen and state == 0:
            log(f"cutscene ended after frame {last[1]}")
            return
        last = (state, frames)
        if button:
            session.press(button)
        session.wait(POLL)
    raise RuntimeError(f"cutscene did not {'end' if seen else 'start'} within {timeout}s (last {last})")


def _wait_frame(session: Session, layout: Layout, frame: int, timeout: float = 60) -> tuple[int, int]:
    for _ in range(round(timeout / POLL)):
        state, frames = _cutscene(session, layout)
        if state and frames >= frame:
            return state, frames
        session.wait(POLL)
    raise RuntimeError(f"cutscene did not reach frame {frame} within {timeout}s")


def _wait_script(session: Session, layout: Layout, script: int, button: str,
                 timeout: float = 120) -> tuple[int, int]:
    """Waits for the cutscene to run `script`, pressing `button` meanwhile to advance its text."""
    for _ in range(round(timeout / POLL)):
        play = _play(session, layout)
        if play and session.u32(play + layout.cutscene_script) == script:
            return _cutscene(session, layout)
        if button:
            session.press(button)
        session.wait(POLL)
    raise RuntimeError(f"cutscene script 0x{script:08X} did not start within {timeout}s")


ARENA_NODE_MAGIC = 0x7373
ARENA_NODE_LIMIT = 100000


def _arena(session: Session, arena: int, listing: Path | None) -> str:
    """An arena's totals from its block list (decomp/oot3d docs/notes/arena.md)."""
    node = session.u32(arena)
    used = free = largest = blocks = 0
    lines = []
    while node and blocks < ARENA_NODE_LIMIT:
        magic, is_free, size, following, _ = struct.unpack("<HHIII", session.read(node, 16))
        if magic != ARENA_NODE_MAGIC:
            return f"block 0x{node:08X} has no magic after {blocks} blocks"
        blocks += 1
        lines.append(f"0x{node:08X} 0x{size:X} {'free' if is_free else 'used'}")
        if is_free:
            free += size
            largest = max(largest, size)
        else:
            used += size
        node = following
    if listing is not None:
        listing.write_text("\n".join(lines) + "\n")
    return f"{blocks} blocks, used 0x{used:X}, free 0x{free:X}, largest free 0x{largest:X}"


WALK_SAMPLE = 0.1
WALK_ARRIVED = 4.0
# The pad eases off inside this distance, down to the weakest push that still moves the player.
WALK_SLOW_WITHIN = 60.0
WALK_SLOWEST = 0.65
STICK_FULL = 32767


def _walk(session: Session, layout: Layout, x: float, z: float, timeout: float, log) -> None:
    """Steers the circle pad toward (x, z): the pad's up is away from the camera's eye."""
    stick = getattr(session, "stick", None)
    if stick is None:
        raise RuntimeError("walk is not available on this backend")
    if layout.camera_eye is None:
        raise RuntimeError("walk needs the camera's layout")
    player = _player(session, layout)
    play = _play(session, layout)
    for _ in range(round(timeout / WALK_SAMPLE)):
        here = struct.unpack("<3f", session.read(player + layout.actor_position, 12))
        log(f"walk {here[0]:.1f} {here[1]:.1f} {here[2]:.1f}")
        away = math.hypot(x - here[0], z - here[2])
        if away <= WALK_ARRIVED:
            return
        eye = struct.unpack("<3f", session.read(play + layout.camera_eye, 12))
        at = struct.unpack("<3f", session.read(play + layout.camera_at, 12))
        # Yaws turn from +z toward +x; the pad's turn from up toward right, which is the other way.
        pad = math.atan2(at[0] - eye[0], at[2] - eye[2]) - math.atan2(x - here[0], z - here[2])
        push = STICK_FULL * min(1.0, max(WALK_SLOWEST, away / WALK_SLOW_WITHIN))
        stick(str(round(math.sin(pad) * push)), str(round(-math.cos(pad) * push)), str(WALK_SAMPLE))
    raise RuntimeError(f"walk did not reach {x}, {z} within {timeout}s (at {here})")


def run(session: Session, steps: list[str], out: Path, layout: Layout) -> None:
    start = time.monotonic()

    def log(text: str) -> None:
        print(f"{time.monotonic() - start:7.1f}s {text}", flush=True)

    for index, step in enumerate(steps):
        kind, _, arg = step.partition(":")
        if kind == "wait":
            session.wait(float(arg))
        elif kind == "shot":
            width, height = session.screenshot(out / f"{index:02d}-{arg}.png")
            log(f"shot {arg} {width}x{height}")
        elif kind == "hold":
            button, seconds = arg.split(",")
            session.hold(button, float(seconds))
            log(f"hold {arg}")
        elif kind == "peek":
            where, size = arg.split(",")
            address = _address(session, layout, where)
            log(f"peek 0x{address:08X} {session.read(address, int(size, 0)).hex()}")
        elif kind == "arena":
            where, _, name = arg.partition(",")
            address = _address(session, layout, where)
            totals = _arena(session, address, out / f"{name}.arena.txt" if name else None)
            log(f"arena 0x{address:08X} {totals}")
        elif kind == "poke":
            where, data = arg.split(",")
            address = _address(session, layout, where)
            session.write(address, bytes.fromhex(data))
            log(f"poke 0x{address:08X} {data}")
        elif kind == "warp":
            play = _play(session, layout)
            session.write(play + layout.next_entrance, struct.pack("<H", int(arg, 0) & 0xFFFF))
            session.write(play + layout.transition_trigger, bytes([TRANSITION_START]))
            log(f"warp to entrance {arg}")
        elif kind == "teleport":
            player = _player(session, layout)
            position = struct.pack("<3f", *(float(part) for part in arg.split(",")))
            for field in (layout.actor_home, layout.actor_position, layout.actor_previous):
                session.write(player + field, position)
            log(f"teleport player 0x{player:08X} to {arg}")
        elif kind == "where":
            player = _player(session, layout)
            x, y, z = struct.unpack("<3f", session.read(player + layout.actor_position, 12))
            log(f"player at {x:.1f} {y:.1f} {z:.1f}")
        elif kind == "walk":
            x, z, *seconds = (float(part) for part in arg.split(","))
            _walk(session, layout, x, z, seconds[0] if seconds else 20.0, log)
        elif kind == "csframe":
            log(f"cutscene at {_wait_frame(session, layout, int(arg, 0))}")
        elif kind == "waitscript":
            script, _, button = arg.partition(",")
            log(f"script {script} at {_wait_script(session, layout, int(script, 0), button)}")
        elif kind == "waitcs":
            seconds, _, button = arg.partition(",")
            _wait_cutscene(session, layout, float(seconds), button, log)
        elif kind == "cs":
            play = _play(session, layout)
            if play == 0:
                log("no play state (title, file select or a scene load)")
                continue
            script = session.u32(play + layout.cutscene_script)
            head = session.read(script, 0x38) if script else b""
            scene = struct.unpack("<h", session.read(play + layout.scene, 2))[0]
            trigger = struct.unpack("<b", session.read(play + layout.transition_trigger, 1))[0]
            log(f"play 0x{play:08X} scene {scene} state/frames {_cutscene(session, layout)} "
                f"trigger {trigger} script 0x{script:08X} head {head.hex()}")
        elif kind == "save":
            data = session.read(layout.save_context, layout.save_size)
            (out / f"{arg}.save.bin").write_bytes(data)
            scene = struct.unpack("<h", session.read(_play(session, layout) + layout.scene, 2))[0]
            log(f"save context {arg} (scene {scene}, cutscene {_cutscene(session, layout)})")
        elif kind in ("savestate", "loadstate", "watch", "hits", "keydown", "keyup", "type", "stick", "cstick", "gyro", "mouse", "device",
                      "record", "stoprecord", "option", "touch", "patch", "textures"):
            extra = getattr(session, kind, None)
            if extra is None:
                raise RuntimeError(f"{kind} is not available on this backend")
            result = extra(*arg.split(",")) if arg else extra()
            log(f"{kind} {arg} {result if result is not None else ''}".rstrip())
        else:
            session.press(kind)
            log(f"press {kind}")
