"""Run a scripted session.

Steps: wait:<s>, shot:<name>, <button>, hold:<button>,<s>, peek:<addr>,<size>,
poke:<addr>,<hex bytes>, warp:<entrance>, waitcs:<s> (wait for a cutscene to start, then end), csframe:<n> (wait until a running cutscene
reaches frame n), save:<name> (dump the save context),
cs (print the cutscene context and script header).
Harness only: savestate:<name>, loadstate:<name>, watch:<addr>,<size>, hits, keydown:<button>,
keyup:<button> (hold across other steps), stick:<x>,<y>,<s> (circle pad, -32767..32767, y up).
"""

from __future__ import annotations

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


def _cutscene(session: Session, layout: Layout) -> tuple[int, int]:
    """(state, frame) of the cutscene context; (0, 0) while no play state exists."""
    play = _play(session, layout)
    if play == 0:
        return 0, 0
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
            address, size = (int(part, 0) for part in arg.split(","))
            log(f"peek 0x{address:08X} {session.read(address, size).hex()}")
        elif kind == "poke":
            address, data = arg.split(",")
            session.write(int(address, 0), bytes.fromhex(data))
            log(f"poke 0x{int(address, 0):08X} {data}")
        elif kind == "warp":
            play = _play(session, layout)
            session.write(play + layout.next_entrance, struct.pack("<h", int(arg, 0)))
            session.write(play + layout.transition_trigger, bytes([TRANSITION_START]))
            log(f"warp to entrance {arg}")
        elif kind == "csframe":
            log(f"cutscene at {_wait_frame(session, layout, int(arg, 0))}")
        elif kind == "waitscript":
            script, _, button = arg.partition(",")
            log(f"script {script} at {_wait_script(session, layout, int(script, 0), button)}")
        elif kind == "waitcs":
            _wait_cutscene(session, layout, float(arg), log)
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
        elif kind in ("savestate", "loadstate", "watch", "hits", "keydown", "keyup", "type", "stick"):
            extra = getattr(session, kind, None)
            if extra is None:
                raise RuntimeError(f"{kind} is not available on this backend")
            result = extra(*arg.split(",")) if arg else extra()
            log(f"{kind} {arg} {result if result is not None else ''}".rstrip())
        else:
            session.press(kind)
            log(f"press {kind}")
