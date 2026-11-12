"""Run a scripted session: steps like wait:5, shot:name, start, hold:a,0.5, peek:0x587958,16."""

from __future__ import annotations

import time
from pathlib import Path

from z3dmods.azahar import Session


def run(session: Session, steps: list[str], out: Path) -> None:
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
        else:
            session.press(kind)
            log(f"press {kind}")
