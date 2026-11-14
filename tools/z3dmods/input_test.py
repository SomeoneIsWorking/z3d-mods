"""Prove the windowed app's input path with real platform events, away from the player's desktop.

The app runs as `z3dmods play` runs it, on a fresh copy of the player's data, inside a headless
Gamescope session; the picker scenario runs the installed desktop shortcut's Exec line itself. Keys and the mouse go in as XTest events on that session's X server, the
gamepad is a uinput device the app finds like any other pad, and frames come from Gamescope's
screenshots. Output: scratch/input-test/<game>/.
"""

from __future__ import annotations

import os
import shlex
import shutil
import signal
import subprocess
import time
from pathlib import Path

from evdev import AbsInfo, UInput, ecodes

from z3dmods import app, config, play, shortcut
from z3dmods.config import Config

WIDTH, HEIGHT = 1280, 720
# The player's Azahar keys (pad_input.cpp kKeys), as X keysyms.
KEYS = {
    "a": "a", "b": "s", "x": "z", "y": "x", "start": "m", "select": "n", "l": "q", "r": "w",
    "menu": "Escape", "reload_patch": "F5", "toggle_patch": "F6", "toggle_textures": "F7", "stick_up": "Up", "stick_down": "Down", "stick_left": "Left", "stick_right": "Right",
}
PAD_BUTTONS = {"a": ecodes.BTN_SOUTH, "b": ecodes.BTN_EAST, "x": ecodes.BTN_NORTH, "y": ecodes.BTN_WEST,
               "start": ecodes.BTN_START, "select": ecodes.BTN_SELECT, "guide": ecodes.BTN_MODE}
# File select's second slot on MM3D's touch screen, which the overlay layout puts beside the top
# screen there: the right two fifths of the window, half its height, centred.
MM3D_SLOT_2 = (0.6 + 0.4 * 0.51, 0.25 + 0.5 * 0.54)
AXIS = AbsInfo(value=0, min=-32768, max=32767, fuzz=16, flat=128, resolution=0)


class Gamepad:
    """A uinput gamepad with standard evdev codes, which SDL maps like a common pad."""

    def __init__(self) -> None:
        self._device = UInput(
            {ecodes.EV_KEY: list(PAD_BUTTONS.values()) + [ecodes.BTN_TL, ecodes.BTN_TR],
             ecodes.EV_ABS: [(ecodes.ABS_X, AXIS), (ecodes.ABS_Y, AXIS), (ecodes.ABS_RX, AXIS), (ecodes.ABS_RY, AXIS)]},
            name="z3d input test pad", vendor=0x045E, product=0x028E, version=0x0110, bustype=ecodes.BUS_USB)

    def close(self) -> None:
        self._device.close()

    def button(self, name: str, seconds: float = 0.15) -> None:
        self._device.write(ecodes.EV_KEY, PAD_BUTTONS[name], 1)
        self._device.syn()
        time.sleep(seconds)
        self._device.write(ecodes.EV_KEY, PAD_BUTTONS[name], 0)
        self._device.syn()

    def stick(self, x: int, y: int, seconds: float) -> None:
        """Left stick, evdev's range (y grows downward), held for `seconds`, then centred."""
        for value in ((x, y), (0, 0)):
            self._device.write(ecodes.EV_ABS, ecodes.ABS_X, value[0])
            self._device.write(ecodes.EV_ABS, ecodes.ABS_Y, value[1])
            self._device.syn()
            if value != (0, 0):
                time.sleep(seconds)


class Session:
    def __init__(self, out: Path, args: list[str], env: dict[str, str] | None = None) -> None:
        self.out = out
        self._args = args
        self._extra_env = env or {}
        self._gamescope: subprocess.Popen | None = None
        self._env: dict[str, str] = {}
        self._shots = 0

    def __enter__(self) -> Session:
        args = self._args
        env = {k: v for k, v in os.environ.items() if k not in ("WAYLAND_DISPLAY", "DISPLAY")}
        env.update(self._extra_env)
        self.out.mkdir(parents=True, exist_ok=True)
        log = (self.out / "gamescope.log").open("wb")
        self._gamescope = subprocess.Popen(
            ["gamescope", "--backend", "headless", "-W", str(WIDTH), "-H", str(HEIGHT), "--", *args],
            env=env, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
        self._env = self._wait_for_app()
        return self

    def __exit__(self, *exc: object) -> None:
        if self._gamescope and self._gamescope.poll() is None:
            os.killpg(self._gamescope.pid, signal.SIGTERM)
            try:
                self._gamescope.wait(timeout=20)
            except subprocess.TimeoutExpired:
                os.killpg(self._gamescope.pid, signal.SIGKILL)
                self._gamescope.wait()

    def _wait_for_app(self) -> dict[str, str]:
        assert self._gamescope
        deadline = time.monotonic() + 60
        while time.monotonic() < deadline:
            if self._gamescope.poll() is not None:
                raise RuntimeError(f"gamescope exited; see {self.out / 'gamescope.log'}")
            for entry in Path("/proc").iterdir():
                if not entry.name.isdigit():
                    continue
                try:
                    if os.getpgid(int(entry.name)) != self._gamescope.pid or (entry / "exe").resolve().name != "z3d":
                        continue
                    items = (entry / "environ").read_bytes().split(b"\0")
                except (ProcessLookupError, FileNotFoundError, PermissionError):
                    continue
                found = dict(item.decode().split("=", 1) for item in items if b"=" in item)
                return {**os.environ, "DISPLAY": found["DISPLAY"],
                        "GAMESCOPE_WAYLAND_DISPLAY": found["GAMESCOPE_WAYLAND_DISPLAY"]}
            time.sleep(0.5)
        raise RuntimeError("z3d did not start inside gamescope")

    def wait(self, seconds: float) -> None:
        assert self._gamescope
        time.sleep(seconds)
        if self._gamescope.poll() is not None:
            raise RuntimeError(f"gamescope exited; see {self.out / 'gamescope.log'}")

    def shot(self, label: str) -> Path:
        self._shots += 1
        path = self.out / f"{self._shots:02d}-{label}.png"
        subprocess.run(["gamescopectl", "screenshot", str(path)], env=self._env, check=True, timeout=20)
        deadline = time.monotonic() + 10
        while not path.exists() and time.monotonic() < deadline:
            time.sleep(0.2)
        return path

    def key(self, button: str, seconds: float = 0.15) -> None:
        keysym = KEYS[button]
        subprocess.run(["xdotool", "keydown", keysym], env=self._env, check=True)
        time.sleep(seconds)
        subprocess.run(["xdotool", "keyup", keysym], env=self._env, check=True)

    def click(self, fx: float, fy: float) -> None:
        """A left click at fraction (fx, fy) of the app window."""
        window = subprocess.run(["xdotool", "getactivewindow"], env=self._env, check=True,
                                capture_output=True, text=True).stdout.strip()
        shell = subprocess.run(["xdotool", "getwindowgeometry", "--shell", window], env=self._env,
                               check=True, capture_output=True, text=True).stdout
        geometry = dict(line.split("=", 1) for line in shell.split())
        x = round(int(geometry["WIDTH"]) * fx)
        y = round(int(geometry["HEIGHT"]) * fy)
        # SDL reads root coordinates under Gamescope's X server, so the window sits at the origin.
        subprocess.run(["xdotool", "windowmove", window, "0", "0"], env=self._env, check=True)
        subprocess.run(["xdotool", "mousemove", str(x), str(y)], env=self._env, check=True)
        time.sleep(0.1)
        subprocess.run(["xdotool", "mousedown", "1"], env=self._env, check=True)
        time.sleep(0.15)
        subprocess.run(["xdotool", "mouseup", "1"], env=self._env, check=True)


def _oot3d(session: Session, pad: Gamepad) -> None:
    """Single screen: keys reach the file select and the player's File 1; keys, then the pad, move Link."""
    session.wait(20)
    session.shot("title")
    session.key("start")
    session.wait(3)
    session.shot("files")
    session.key("a")
    session.wait(2)
    session.key("a")
    session.wait(10)
    session.shot("house")
    session.key("stick_left", 1.2)
    session.wait(1)
    session.shot("keys-moved-link")
    pad.stick(32767, 0, 1.2)
    session.wait(1)
    session.shot("pad-moved-link")


def _mm3d(session: Session, pad: Gamepad) -> None:
    """The overlay layout: keys reach file select, a tap on slot 2 makes a file, Start skips the opening."""
    session.wait(30)
    session.key("start")
    session.wait(12)
    session.shot("title")
    session.key("a")
    session.wait(5)
    session.shot("files")
    session.click(*MM3D_SLOT_2)
    session.wait(4)
    session.shot("tapped-slot-2")
    session.key("a")
    session.wait(6)
    session.shot("narration")
    session.key("start")
    session.wait(5)
    session.shot("skipped-to-woods")


def _hotswap(session: Session, pad: Gamepad) -> None:
    """MM3D's mods swapped while it runs: F7 turns the textures off and on, F6 takes the patch out
    and puts it back, F5 reads it again; the game runs on (the log names each swap)."""
    _mm3d(session, pad)
    session.wait(25)
    session.shot("woods")
    for key in ("toggle_textures", "toggle_textures", "toggle_patch", "toggle_patch", "reload_patch"):
        session.key(key)
        session.wait(3)
        session.shot(key)


def _picker(session: Session, pad: Gamepad) -> None:
    """The shortcut's picker: keys boot OoT3D and Escape returns; the pad boots MM3D and Guide
    returns; a click boots OoT3D again."""
    session.wait(8)
    session.shot("picker")
    session.key("a")
    session.wait(20)
    session.shot("oot3d")
    session.key("menu")
    session.wait(4)
    session.shot("picker-after-oot3d")
    pad.stick(32767, 0, 0.3)
    session.wait(1)
    session.shot("picker-mm3d-selected")
    pad.button("a")
    session.wait(30)
    session.shot("mm3d")
    pad.button("guide")
    session.wait(4)
    session.shot("picker-after-mm3d")
    session.click(0.3, 0.5)
    session.wait(20)
    session.shot("oot3d-from-click")


def _options(session: Session, pad: Gamepad) -> None:
    """The picker's options: X on MM3D opens them, A turns the cutscene skip off, and Start then
    leaves the opening narration running."""
    session.wait(8)
    session.key("stick_right", 0.3)
    session.wait(1)
    session.key("x")
    session.wait(1)
    session.shot("options")
    session.key("a")
    session.wait(1)
    session.shot("skip-off")
    session.key("b")
    session.wait(1)
    session.shot("picker")
    session.key("a")
    session.wait(30)
    session.key("start")
    session.wait(12)
    session.key("a")
    session.wait(5)
    session.click(*MM3D_SLOT_2)
    session.wait(4)
    session.key("a")
    session.wait(6)
    session.shot("narration")
    session.key("start")
    session.wait(5)
    session.shot("narration-after-start")


def _options_oot3d(session: Session, pad: Gamepad) -> None:
    """The picker's options for OoT3D: X opens them, Down and A turn Quick Text off, and the text
    Link reads beside his bed in File 1 then types itself out."""
    session.wait(8)
    session.key("x")
    session.wait(1)
    session.shot("options")
    session.key("stick_down")
    session.wait(1)
    session.key("a")
    session.wait(1)
    session.shot("quick-text-off")
    session.key("b")
    session.wait(1)
    session.key("a")
    session.wait(20)
    session.key("start")
    session.wait(3)
    session.key("a")
    session.wait(2)
    session.key("a")
    session.wait(10)
    session.shot("house")
    session.key("a", 0.05)
    for shot in ("text-1", "text-2", "text-3"):
        session.shot(shot)
    session.wait(4)
    session.shot("text-late")


# Direct launches of one game, and the installed shortcut's picker.
DIRECT = {"oot3d": _oot3d, "mm3d": _mm3d, "hotswap": _hotswap}
# The game a direct scenario launches, when it is not the scenario's name.
DIRECT_GAME = {"hotswap": "mm3d"}
SCENARIOS = {**DIRECT, "picker": _picker, "options": _options, "options-oot3d": _options_oot3d}


def _session(cfg: Config, scenario: str, out: Path) -> Session:
    user = out / "user"
    app.seed_user(cfg, user, fresh=True)
    if scenario in DIRECT:
        return Session(out, play.launch_args(cfg, DIRECT_GAME.get(scenario, scenario), user))
    entry = Path.home() / ".local/share/applications" / f"{play.DESKTOP_ID}.desktop"
    if not entry.exists():
        raise RuntimeError(f"{entry} is missing; run z3dmods shortcut")
    return Session(out, shlex.split(shortcut.desktop_exec(entry)), {config.APP_DATA_ENV: str(user)})


def run(cfg: Config, scenario: str) -> Path:
    out = cfg.repo / "scratch" / "input-test" / scenario
    if out.exists():
        shutil.rmtree(out)
    pad = Gamepad()
    try:
        with _session(cfg, scenario, out) as session:
            SCENARIOS[scenario](session, pad)
    finally:
        pad.close()
    return out
