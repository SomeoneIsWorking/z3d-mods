"""Drive the player's Azahar headless: a Gamescope headless session, XTest keys, RPC RAM and frames."""

from __future__ import annotations

import os
import signal
import socket
import struct
import subprocess
import time
from pathlib import Path

from z3dmods import azahar_config
from z3dmods.config import Config

RPC_PORT = 45987
RPC_VERSION = 2
READ_MEMORY, WRITE_MEMORY, TAKE_SCREENSHOT, READ_SCREENSHOT = 1, 2, 5, 6
MAX_READ = 32 * 1024

# The player's keyboard profile (qt-config.ini [Controls] profiles\1), as X keysyms.
KEYS = {
    "a": "a", "b": "s", "x": "z", "y": "x", "start": "m", "select": "n",
    "l": "q", "r": "w", "zl": "1", "zr": "2",
    "up": "t", "down": "g", "left": "f", "right": "h",
    "stick_up": "Up", "stick_down": "Down", "stick_left": "Left", "stick_right": "Right",
}


class Rpc:
    def __init__(self) -> None:
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._sock.settimeout(15)
        self._id = 0

    def close(self) -> None:
        self._sock.close()

    def request(self, kind: int, payload: bytes) -> bytes:
        self._id += 1
        header = struct.pack("<IIII", RPC_VERSION, self._id, kind, len(payload))
        self._sock.sendto(header + payload, ("127.0.0.1", RPC_PORT))
        while True:
            reply, _ = self._sock.recvfrom(16 + MAX_READ)
            _, reply_id, _, size = struct.unpack_from("<IIII", reply)
            if reply_id == self._id:
                return reply[16:16 + size]

    def read(self, address: int, size: int) -> bytes:
        out = bytearray()
        while len(out) < size:
            chunk = min(MAX_READ, size - len(out))
            data = self.request(READ_MEMORY, struct.pack("<II", address + len(out), chunk))
            if len(data) != chunk:
                raise RuntimeError(f"rpc: read 0x{address + len(out):08X} returned {len(data)} bytes")
            out += data
        return bytes(out)

    def u32(self, address: int) -> int:
        return struct.unpack("<I", self.read(address, 4))[0]

    def write(self, address: int, data: bytes) -> None:
        self.request(WRITE_MEMORY, struct.pack("<II", address, len(data)) + data)

    def screenshot(self, path: Path, scale: int = 1) -> tuple[int, int]:
        info = self.request(TAKE_SCREENSHOT, struct.pack("<II", scale, 0))
        result, width, height, size = struct.unpack("<IIII", info)
        if result != 0:
            raise RuntimeError(f"rpc: screenshot failed with result {result}")
        png = bytearray()
        while len(png) < size:
            png += self.request(READ_SCREENSHOT, struct.pack("<II", len(png), min(MAX_READ, size - len(png))))
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(png)
        return width, height


class Session:
    """One Azahar run on the player's data inside a headless Gamescope."""

    def __init__(self, cfg: Config, rom: Path, log: Path,
                 run_settings: dict[str, dict[str, object]] | None = None) -> None:
        """run_settings: global Azahar settings for this run only, restored afterwards."""
        self._cfg = cfg
        self._run_settings = run_settings or {}
        self._rom = rom
        self._log_path = log
        self._gamescope: subprocess.Popen | None = None
        self._display = ""
        self._config_snapshot: dict[Path, bytes] = {}
        self.config_changes: dict[str, list[str]] = {}
        self.rpc = Rpc()

    def __enter__(self) -> Session:
        self._config_snapshot = {p: p.read_bytes() for p in self._cfg.azahar_config.rglob("*.ini")}
        if self._run_settings:
            azahar_config.set_global(self._cfg.azahar_config, self._run_settings)
        env = {k: v for k, v in os.environ.items() if k not in ("WAYLAND_DISPLAY", "DISPLAY")}
        env["QT_QPA_PLATFORM"] = "xcb"
        self._log_path.parent.mkdir(parents=True, exist_ok=True)
        log = self._log_path.open("wb")
        self._gamescope = subprocess.Popen(
            ["gamescope", "--backend", "headless", "-W", "1280", "-H", "720", "--",
             str(self._cfg.azahar_appimage), "-f", str(self._rom)],
            env=env, stdout=log, stderr=subprocess.STDOUT, start_new_session=True,
        )
        self._display = self._wait_for_display()
        self._wait_for_rpc()
        return self

    def __exit__(self, *exc: object) -> None:
        self.rpc.close()
        if self._gamescope and self._gamescope.poll() is None:
            os.killpg(self._gamescope.pid, signal.SIGTERM)
            try:
                self._gamescope.wait(timeout=20)
            except subprocess.TimeoutExpired:
                os.killpg(self._gamescope.pid, signal.SIGKILL)
                self._gamescope.wait()
        self._restore_config()

    def _restore_config(self) -> None:
        """Put the player's Azahar config back; Azahar rewrites it on boot and exit."""
        for path in self._cfg.azahar_config.rglob("*.ini"):
            if path not in self._config_snapshot:
                path.unlink()
        for path, data in self._config_snapshot.items():
            now = path.read_bytes() if path.exists() else b""
            if now != data:
                before = set(data.decode(errors="replace").splitlines())
                self.config_changes[path.name] = [
                    line for line in now.decode(errors="replace").splitlines() if line not in before]
                path.write_bytes(data)

    def _children(self) -> list[int]:
        assert self._gamescope
        pgid = self._gamescope.pid
        pids = []
        for entry in Path("/proc").iterdir():
            if entry.name.isdigit():
                try:
                    if os.getpgid(int(entry.name)) == pgid:
                        pids.append(int(entry.name))
                except ProcessLookupError:
                    continue
        return pids

    def _wait_for_display(self) -> str:
        deadline = time.monotonic() + 60
        while time.monotonic() < deadline:
            for pid in self._children():
                try:
                    exe = Path(f"/proc/{pid}/exe").resolve().name
                    environ = Path(f"/proc/{pid}/environ").read_bytes().split(b"\0")
                except (FileNotFoundError, PermissionError):
                    continue
                if exe == "azahar":
                    for item in environ:
                        if item.startswith(b"DISPLAY="):
                            return item[8:].decode()
            self._check_alive()
            time.sleep(0.5)
        raise RuntimeError("azahar did not start inside gamescope")

    def _wait_for_rpc(self) -> None:
        deadline = time.monotonic() + 90
        self.rpc._sock.settimeout(1)
        while time.monotonic() < deadline:
            self._check_alive()
            try:
                self.rpc.read(0x00100000, 4)
                self.rpc._sock.settimeout(15)
                return
            except (TimeoutError, RuntimeError):
                continue
        raise RuntimeError("azahar RPC server did not answer")

    def _check_alive(self) -> None:
        assert self._gamescope
        if self._gamescope.poll() is not None:
            raise RuntimeError(f"gamescope exited with {self._gamescope.returncode}; see {self._log_path}")

    def press(self, button: str, hold: float = 0.2) -> None:
        self.hold(button, hold)

    def hold(self, button: str, seconds: float) -> None:
        key = KEYS[button]
        env = dict(os.environ, DISPLAY=self._display)
        subprocess.run(["xdotool", "keydown", key], env=env, check=True)
        time.sleep(seconds)
        subprocess.run(["xdotool", "keyup", key], env=env, check=True)

    def wait(self, seconds: float) -> None:
        self._check_alive()
        time.sleep(seconds)
        self._check_alive()
