"""Run a scripted session.

Steps: wait:<s>, shot:<name>, <button>, hold:<button>,<s>, peek:<addr>,<size>, warp:<entrance>,
waitcs:<s> (wait for a cutscene to start, then end), save:<name> (dump the save context),
cs (print the cutscene context and script header).
"""

from __future__ import annotations

import struct
import time
from pathlib import Path

from z3dmods.azahar import Session
from z3dmods.games import TRANSITION_START, Layout


def _play(session: Session, layout: Layout) -> int:
    return session.rpc.u32(layout.play_pointer)


def _cutscene(session: Session, layout: Layout) -> tuple[int, int]:
    play = _play(session, layout)
    state = session.rpc.read(play + layout.cutscene_state, 1)[0]
    frames = struct.unpack("<H", session.rpc.read(play + layout.cutscene_frames, 2))[0]
    return state, frames


def _wait_cutscene(session: Session, layout: Layout, timeout: float, log) -> None:
    deadline = time.monotonic() + timeout
    seen = False
    last = (0, 0)
    while time.monotonic() < deadline:
        state, frames = _cutscene(session, layout)
        if state and not seen:
            log(f"cutscene running (state {state})")
            seen = True
        if seen and state == 0:
            log(f"cutscene ended after frame {last[1]}")
            return
        last = (state, frames)
        session.wait(0.1)
    raise RuntimeError(f"cutscene did not {'end' if seen else 'start'} within {timeout}s (last {last})")


def run(session: Session, steps: list[str], out: Path, layout: Layout) -> None:
    start = time.monotonic()

    def log(text: str) -> None:
        print(f"{time.monotonic() - start:7.1f}s {text}", flush=True)

    for index, step in enumerate(steps):
        kind, _, arg = step.partition(":")
        if kind == "wait":
            session.wait(float(arg))
        elif kind == "shot":
            width, height = session.rpc.screenshot(out / f"{index:02d}-{arg}.png")
            log(f"shot {arg} {width}x{height}")
        elif kind == "hold":
            button, seconds = arg.split(",")
            session.hold(button, float(seconds))
            log(f"hold {arg}")
        elif kind == "peek":
            address, size = (int(part, 0) for part in arg.split(","))
            log(f"peek 0x{address:08X} {session.rpc.read(address, size).hex()}")
        elif kind == "warp":
            play = _play(session, layout)
            session.rpc.write(play + layout.next_entrance, struct.pack("<h", int(arg, 0)))
            session.rpc.write(play + layout.transition_trigger, bytes([TRANSITION_START]))
            log(f"warp to entrance {arg}")
        elif kind == "waitcs":
            _wait_cutscene(session, layout, float(arg), log)
        elif kind == "cs":
            play = _play(session, layout)
            context = session.rpc.read(play + layout.cutscene_state - 8, 0x30)
            script = struct.unpack_from("<I", context, 4)[0]
            head = session.rpc.read(script, 0x10) if script else b""
            scene = struct.unpack("<h", session.rpc.read(play + layout.scene, 2))[0]
            log(f"play 0x{play:08X} scene {scene} state/frames {_cutscene(session, layout)} "
                f"csCtx {context.hex()} script 0x{script:08X} head {head.hex()}")
        elif kind == "save":
            data = session.rpc.read(layout.save_context, layout.save_size)
            (out / f"{arg}.save.bin").write_bytes(data)
            scene = struct.unpack("<h", session.rpc.read(_play(session, layout) + layout.scene, 2))[0]
            log(f"save context {arg} (scene {scene}, cutscene {_cutscene(session, layout)})")
        else:
            session.press(kind)
            log(f"press {kind}")
