"""Drive the libretro harness (harness/, build/harness/z3d_harness) on a copy of the player's data.

The core's user dir is scratch/harness/<game>/Azahar: each run copies in the player's nand, sdmc and
sysdata, and links the installed load/ folder (mods and textures, which the core only reads), so the
player's own data is only read.
"""

from __future__ import annotations

import shutil
import subprocess
from pathlib import Path

from z3dmods.config import Config

FPS = 60
# RETRO_DEVICE_ID_JOYPAD bits; the Azahar core maps face buttons by position, so libretro A (east)
# is the 3DS A.
BUTTONS = {
    "b": 0, "y": 1, "select": 2, "start": 3, "up": 4, "down": 5, "left": 6, "right": 7,
    "a": 8, "x": 9, "l": 10, "r": 11, "zl": 12, "zr": 13,
}
PRESS_FRAMES = 6
USER_DATA = ("nand", "sdmc", "sysdata")
READ_ONLY_DATA = ("load",)
OPTIONS = {
    "citra_graphics_api": "OpenGL",
    "citra_use_libretro_save_path": "LibRetro Default",
}


class HarnessSession:
    def __init__(self, cfg: Config, rom: Path, game: str, screen_layout: str, custom_textures: bool,
                 log: Path, core_options: dict[str, str] | None = None) -> None:
        if cfg.libretro_core is None:
            raise RuntimeError('set "libretro_core" in env-assets/local.json to the built azahar_libretro.so')
        self._cfg = cfg
        self._rom = rom
        self._user = cfg.repo / "scratch" / "harness" / game
        self._states = cfg.repo / "scratch" / "harness" / "states" / game
        self._log_path = log
        self._options = dict(OPTIONS, citra_layout_option=screen_layout,
                             citra_custom_textures="enabled" if custom_textures else "disabled")
        self._options.update(core_options or {})
        self._process: subprocess.Popen | None = None
        self._held = 0

    def __enter__(self) -> HarnessSession:
        self._copy_player_data()
        binary = self._cfg.repo / "build" / "harness" / "z3d_harness"
        if not binary.exists():
            raise RuntimeError(f"{binary} missing: cmake -S harness -B build/harness -G Ninja && ninja -C build/harness")
        args = [str(binary), "--core", str(self._cfg.libretro_core), "--rom", str(self._rom), "--user", str(self._user)]
        for key, value in self._options.items():
            args += ["--option", f"{key}={value}"]
        self._log_path.parent.mkdir(parents=True, exist_ok=True)
        self._log = self._log_path.open("wb")
        self._process = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                         stderr=self._log, text=True, bufsize=1)
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

    def _copy_player_data(self) -> None:
        target = self._user / "Azahar"
        if target.exists():
            shutil.rmtree(target)
        target.mkdir(parents=True)
        for name in USER_DATA:
            source = self._cfg.azahar_data / name
            if source.exists():
                shutil.copytree(source, target / name, symlinks=True)
        for name in READ_ONLY_DATA:
            source = self._cfg.azahar_data / name
            if source.exists():
                (target / name).symlink_to(source, target_is_directory=True)

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
