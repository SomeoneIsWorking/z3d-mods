"""Run a scripted session.

Steps: wait:<s>, shot:<name>, <button>, hold:<button>,<s>, peek:<addr>,<size>, warp:<entrance>,
waitcs:<s> (wait for a cutscene to start, then end), save:<name> (dump the save context),
cs (print the cutscene context and script header).
Harness only: savestate:<name>, loadstate:<name>, watch:<addr>,<size>, hits.
"""

from __future__ import annotations

import struct
import time
from pathlib import Path

from typing import Protocol

from z3dmods.games import TRANSITION_START, Layout


class Session(Protocol):
    """What a run needs from a backend: the standalone Azahar or the libretro harness."""

    def read(self, address: int, size: int) -> bytes: ...
    def u32(self, address: int) -> int: ...
    def write(self, address: int, data: bytes) -> None: ...
    def screenshot(self, path: Path) -> tuple[int, int]: ...
    def wait(self, seconds: float) -> None: ...
    def hold(self, button: str, seconds: float) -> None: ...
    def press(self, button: str) -> None: ...


def _play(session: Session, layout: Layout | None) -> int:
    if layout is None:
        raise RuntimeError("this game has no RAM layout in tools/z3dmods/games.py yet")
    return session.u32(layout.play_pointer)


def _cutscene(session: Session, layout: Layout) -> tuple[int, int]:
    play = _play(session, layout)
    state = session.read(play + layout.cutscene_state, 1)[0]
    frames = struct.unpack("<H", session.read(play + layout.cutscene_frames, 2))[0]
    return state, frames


POLL = 0.1


def _wait_cutscene(session: Session, layout: Layout, timeout: float, log) -> None:
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
        session.wait(POLL)
    raise RuntimeError(f"cutscene did not {'end' if seen else 'start'} within {timeout}s (last {last})")


def run(session: Session, steps: list[str], out: Path, layout: Layout | None) -> None:
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
            address, size = (int(part, 0) for part in arg.split(","))
            log(f"peek 0x{address:08X} {session.read(address, size).hex()}")
        elif kind == "warp":
            play = _play(session, layout)
            session.write(play + layout.next_entrance, struct.pack("<h", int(arg, 0)))
            session.write(play + layout.transition_trigger, bytes([TRANSITION_START]))
            log(f"warp to entrance {arg}")
        elif kind == "waitcs":
            _wait_cutscene(session, layout, float(arg), log)
        elif kind == "cs":
            play = _play(session, layout)
            context = session.read(play + layout.cutscene_state - 8, 0x30)
            script = struct.unpack_from("<I", context, 4)[0]
            head = session.read(script, 0x10) if script else b""
            scene = struct.unpack("<h", session.read(play + layout.scene, 2))[0]
            log(f"play 0x{play:08X} scene {scene} state/frames {_cutscene(session, layout)} "
                f"csCtx {context.hex()} script 0x{script:08X} head {head.hex()}")
        elif kind == "save":
            if layout is None:
                raise RuntimeError("this game has no RAM layout in tools/z3dmods/games.py yet")
            data = session.read(layout.save_context, layout.save_size)
            (out / f"{arg}.save.bin").write_bytes(data)
            scene = struct.unpack("<h", session.read(_play(session, layout) + layout.scene, 2))[0]
            log(f"save context {arg} (scene {scene}, cutscene {_cutscene(session, layout)})")
        elif kind in ("savestate", "loadstate", "watch", "hits"):
            extra = getattr(session, kind, None)
            if extra is None:
                raise RuntimeError(f"{kind} needs the harness backend (--harness)")
            result = extra(*arg.split(",")) if arg else extra()
            log(f"{kind} {arg} {result if result is not None else ''}".rstrip())
        else:
            session.press(kind)
            log(f"press {kind}")
