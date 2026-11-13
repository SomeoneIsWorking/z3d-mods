"""Drive the z3d app headless as the frame-exact harness.

Each run gets a fresh user folder, scratch/harness/<name> (app.seed_user), so the player's own data
is only read.
"""

from __future__ import annotations

import os
import subprocess
from pathlib import Path

from z3dmods import app
from z3dmods.config import Config
from z3dmods.install import GameSet

FPS = 60
# Azahar's Settings::NativeButton order: the bits of the app's `input` command.
BUTTONS = {
    "a": 0, "b": 1, "x": 2, "y": 3, "up": 4, "down": 5, "left": 6, "right": 7,
    "l": 8, "r": 9, "start": 10, "select": 11, "zl": 14, "zr": 15, "home": 16,
}
PRESS_FRAMES = 6
# The tools render at the 3DS's own size unless a run asks for more: small screenshots.
RESOLUTION = 1


class HarnessSession:
    def __init__(self, cfg: Config, game: GameSet, name: str, log: Path,
                 textures: bool | None = None, dump_textures: bool = False,
                 resolution: int = RESOLUTION) -> None:
        self._cfg = cfg
        self._resolution = resolution
        self._game = game
        self._textures = textures
        self._dump_textures = dump_textures
        self._user = cfg.repo / "scratch" / "harness" / name
        self._states = cfg.repo / "scratch" / "harness" / "states" / name
        self._log_path = log
        self._process: subprocess.Popen | None = None
        self._held = 0

    def __enter__(self) -> HarnessSession:
        app.seed_user(self._cfg, self._user, fresh=True)
        args = app.launch_args(self._cfg, self._game, self._user, self._resolution, self._textures) + ["--hidden"]
        if self._dump_textures:
            args.append("--dump-textures")
        self._log_path.parent.mkdir(parents=True, exist_ok=True)
        self._log = self._log_path.open("wb")
        env = dict(os.environ, **self._cfg.session_env)
        self._process = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                         stderr=self._log, text=True, bufsize=1, env=env)
        ready = self._process.stdout.readline().strip()
        if ready != "ready":
            raise RuntimeError(f"harness did not start ({ready!r}); see {self._log_path}")
        return self

    def __exit__(self, *exc: object) -> None:
        if self._process and self._process.poll() is None:
            try:
                self._send("quit")
            except RuntimeError:
                pass
            try:
                self._process.wait(timeout=30)
            except subprocess.TimeoutExpired:
                self._process.kill()
                self._process.wait()
        self._log.close()

    def _send(self, line: str) -> str:
        assert self._process and self._process.stdin and self._process.stdout
        self._process.stdin.write(line + "\n")
        self._process.stdin.flush()
        reply = self._process.stdout.readline().strip()
        if not reply:
            raise RuntimeError(f"harness exited on {line!r}; see {self._log_path}")
        status, _, value = reply.partition(" ")
        if status != "ok":
            raise RuntimeError(f"harness: {line!r}: {value}")
        return value

    def frames(self) -> int:
        return int(self._send("frames"))

    def read(self, address: int, size: int) -> bytes:
        return bytes.fromhex(self._send(f"r {address:x} {size:x}"))

    def u32(self, address: int) -> int:
        return int.from_bytes(self.read(address, 4), "little")

    def write(self, address: int, data: bytes) -> None:
        self._send(f"w {address:x} {data.hex()}")

    def screenshot(self, path: Path) -> tuple[int, int]:
        path.parent.mkdir(parents=True, exist_ok=True)
        width, height = self._send(f"shot {path}").split("x")
        return int(width), int(height)

    def run_frames(self, count: int) -> None:
        if count > 0:
            self._send(f"run {count}")

    def wait(self, seconds: float) -> None:
        self.run_frames(round(seconds * FPS))

    def hold(self, button: str, seconds: float) -> None:
        self.keydown(button)
        self.run_frames(max(1, round(seconds * FPS)))
        self.keyup(button)

    def keydown(self, button: str) -> str:
        self._held |= 1 << BUTTONS[button]
        return self._send(f"input {self._held:x}")

    def keyup(self, button: str) -> str:
        self._held &= ~(1 << BUTTONS[button])
        return self._send(f"input {self._held:x}")

    def stick(self, x: str, y: str, seconds: str) -> str:
        """Holds the circle pad at (x, y), each -32767..32767 with y up, for `seconds`."""
        self._send(f"stick {int(x)} {int(y)}")
        self.run_frames(max(1, round(float(seconds) * FPS)))
        return self._send("stick 0 0")

    def press(self, button: str) -> None:
        self.hold(button, PRESS_FRAMES / FPS)

    def savestate(self, name: str) -> str:
        self._states.mkdir(parents=True, exist_ok=True)
        return self._send(f"savestate {self._states / name}.state") + " bytes"

    def loadstate(self, name: str) -> str:
        return self._send(f"loadstate {self._states / name}.state")

    def watch(self, address: str, size: str) -> str:
        return self._send(f"watch {int(address, 0):x} {int(size, 0):x}")

    def hits(self) -> str:
        return self._send("hits")
