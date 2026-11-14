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
import threading
import time
from dataclasses import replace
from pathlib import Path

from evdev import AbsInfo, UInput, ecodes

from z3dmods import app, config, ctr, desktop_container, games, harness, package, play, shortcut, source_check, steps, symbols
from z3dmods.app_keys import AppKeys
from z3dmods.config import Config
from z3dmods.install import GameSet

WIDTH, HEIGHT = 1280, 720
# The scenarios run in a smaller window than WIDTH by HEIGHT.
SMALL = (854, 480)
# The key the shared setup screen (setup-ui) takes a folder question's answer on: its own, not
# one of the app's tables.
SETUP_UI_CONFIRM = "Return"
# The top screen, which the single-screen layouts fit into the window.
TOP_SCREEN = (400, 240)
# Every button of an Xbox 360 pad in the kernel's codes, so SDL numbers them as its mapping does.
PAD_BUTTONS = {"a": ecodes.BTN_SOUTH, "b": ecodes.BTN_EAST, "x": ecodes.BTN_NORTH, "y": ecodes.BTN_WEST,
               "lb": ecodes.BTN_TL, "rb": ecodes.BTN_TR,
               "start": ecodes.BTN_START, "select": ecodes.BTN_SELECT, "guide": ecodes.BTN_MODE,
               "ls": ecodes.BTN_THUMBL, "rs": ecodes.BTN_THUMBR}
# The D-pad is a hat: its axis and the way each direction pushes it.
PAD_DPAD = {"up": (ecodes.ABS_HAT0Y, -1), "down": (ecodes.ABS_HAT0Y, 1),
            "left": (ecodes.ABS_HAT0X, -1), "right": (ecodes.ABS_HAT0X, 1)}
HAT = AbsInfo(value=0, min=-1, max=1, fuzz=0, flat=0, resolution=0)
TRIGGER = AbsInfo(value=0, min=0, max=255, fuzz=0, flat=0, resolution=0)
# File select's second slot on MM3D's touch screen, which the overlay layout puts beside the top
# screen there: the right two fifths of the window, half its height, centred.
MM3D_SLOT_2 = (0.6 + 0.4 * 0.51, 0.25 + 0.5 * 0.54)
AXIS = AbsInfo(value=0, min=-32768, max=32767, fuzz=16, flat=128, resolution=0)


class Gamepad:
    """A uinput gamepad with standard evdev codes, which SDL maps like a common pad."""

    def __init__(self) -> None:
        self._device = UInput(
            {ecodes.EV_KEY: list(PAD_BUTTONS.values()),
             ecodes.EV_ABS: [(ecodes.ABS_X, AXIS), (ecodes.ABS_Y, AXIS), (ecodes.ABS_Z, TRIGGER),
                             (ecodes.ABS_RX, AXIS), (ecodes.ABS_RY, AXIS), (ecodes.ABS_RZ, TRIGGER),
                             (ecodes.ABS_HAT0X, HAT), (ecodes.ABS_HAT0Y, HAT)]},
            name="z3d input test pad", vendor=0x045E, product=0x028E, version=0x0110, bustype=ecodes.BUS_USB)

    def close(self) -> None:
        self._device.close()

    def hold(self, name: str, down: bool) -> None:
        self._device.write(ecodes.EV_KEY, PAD_BUTTONS[name], int(down))
        self._device.syn()

    def button(self, name: str, seconds: float = 0.15) -> None:
        self.hold(name, True)
        time.sleep(seconds)
        self.hold(name, False)

    def dpad(self, direction: str, seconds: float = 0.15) -> None:
        axis, push = PAD_DPAD[direction]
        for value in (push, 0):
            self._device.write(ecodes.EV_ABS, axis, value)
            self._device.syn()
            if value:
                time.sleep(seconds)

    def stick(self, x: int, y: int, seconds: float) -> None:
        """Left stick, evdev's range (y grows downward), held for `seconds`, then centred."""
        for value in ((x, y), (0, 0)):
            self._device.write(ecodes.EV_ABS, ecodes.ABS_X, value[0])
            self._device.write(ecodes.EV_ABS, ecodes.ABS_Y, value[1])
            self._device.syn()
            if value != (0, 0):
                time.sleep(seconds)


class Session:
    def __init__(self, out: Path, args: list[str], keys: AppKeys, env: dict[str, str] | None = None,
                 program: str = "z3d", size: tuple[int, int] = (WIDTH, HEIGHT)) -> None:
        """`program` is the one Gamescope starts whose environment names the display: the app,
        or what stands between Gamescope and it. `keys` are the app's bindings."""
        self.out = out
        self._keys = keys
        self._size = size
        self._args = args
        self._program = program
        self._extra_env = env or {}
        self._gamescope: subprocess.Popen | None = None
        self._env: dict[str, str] = {}
        self._shots = 0
        self._log_read = 0

    def __enter__(self) -> Session:
        args = self._args
        env = {k: v for k, v in os.environ.items() if k not in ("WAYLAND_DISPLAY", "DISPLAY")}
        env.update(self._extra_env)
        self.out.mkdir(parents=True, exist_ok=True)
        log = (self.out / "gamescope.log").open("wb")
        self._gamescope = subprocess.Popen(
            ["gamescope", "--backend", "headless", "-W", str(self._size[0]), "-H", str(self._size[1]), "--", *args],
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
                    if os.getpgid(int(entry.name)) != self._gamescope.pid or (entry / "exe").resolve().name != self._program:
                        continue
                    items = (entry / "environ").read_bytes().split(b"\0")
                except (ProcessLookupError, FileNotFoundError, PermissionError):
                    continue
                found = dict(item.decode().split("=", 1) for item in items if b"=" in item)
                return {**os.environ, "DISPLAY": found["DISPLAY"],
                        "GAMESCOPE_WAYLAND_DISPLAY": found["GAMESCOPE_WAYLAND_DISPLAY"]}
            time.sleep(0.5)
        raise RuntimeError(f"{self._program} did not start inside gamescope")

    def wait(self, seconds: float) -> None:
        assert self._gamescope
        time.sleep(seconds)
        if self._gamescope.poll() is not None:
            raise RuntimeError(f"gamescope exited; see {self.out / 'gamescope.log'}")

    def expect(self, text: str, seconds: float = 30) -> str:
        """Waits for the app to log a line holding `text` after the last line expected, and
        returns it: the menus log what they are on (app/src/title_picker.cpp, options_menu.cpp)."""
        assert self._gamescope
        log = self.out / "gamescope.log"
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            data = log.read_bytes()
            while (end := data.find(b"\n", self._log_read)) >= 0:
                line = data[self._log_read:end].decode(errors="replace")
                self._log_read = end + 1
                if text in line:
                    return line
            if self._gamescope.poll() is not None:
                raise RuntimeError(f"gamescope exited; see {log}")
            time.sleep(0.05)
        raise RuntimeError(f"the app did not log {text!r} within {seconds}s; see {log}")

    def quiet(self, text: str, seconds: float) -> None:
        """Lets `seconds` pass and refuses a line holding `text` logged in them."""
        self.wait(seconds)
        data = (self.out / "gamescope.log").read_bytes()[self._log_read:].decode(errors="replace")
        if text in data:
            raise RuntimeError(f"the app logged {text!r}; see {self.out / 'gamescope.log'}")

    def shot(self, label: str) -> Path:
        self._shots += 1
        path = self.out / f"{self._shots:02d}-{label}.png"
        subprocess.run(["gamescopectl", "screenshot", str(path)], env=self._env, check=True, timeout=20)
        deadline = time.monotonic() + 10
        while not path.exists() and time.monotonic() < deadline:
            time.sleep(0.2)
        return path

    def press(self, keysyms: list[str], seconds: float = 0.15) -> None:
        """X keys held together for `seconds`, pressed in order."""
        for keysym in keysyms:
            subprocess.run(["xdotool", "keydown", keysym], env=self._env, check=True)
        time.sleep(seconds)
        for keysym in reversed(keysyms):
            subprocess.run(["xdotool", "keyup", keysym], env=self._env, check=True)

    def key(self, table: str, name: str, seconds: float = 0.15) -> None:
        """The key the app binds to `name` of `table` (app_keys.py): menu, name, play, slot, stick
        or window."""
        self.press(self._keys.keysyms(table, name), seconds)

    def mouse_button(self, verb: str, seconds: float = 0.15) -> None:
        """The mouse button the app binds to the play verb `verb`."""
        button = str(self._keys.mouse_button(verb))
        subprocess.run(["xdotool", "mousedown", button], env=self._env, check=True)
        time.sleep(seconds)
        subprocess.run(["xdotool", "mouseup", button], env=self._env, check=True)

    def mouse(self, dx: int, dy: int) -> None:
        """The mouse moved by (dx, dy) counts, x right and y down."""
        subprocess.run(["xdotool", "mousemove_relative", "--", str(dx), str(dy)], env=self._env, check=True)

    def _window(self) -> tuple[str, int, int]:
        window = subprocess.run(["xdotool", "getactivewindow"], env=self._env, check=True,
                                capture_output=True, text=True).stdout.strip()
        shell = subprocess.run(["xdotool", "getwindowgeometry", "--shell", window], env=self._env,
                               check=True, capture_output=True, text=True).stdout
        geometry = dict(line.split("=", 1) for line in shell.split())
        return window, int(geometry["WIDTH"]), int(geometry["HEIGHT"])

    def click_top(self, x: float, y: float) -> None:
        """A left click at a point of the top screen's 400x240, which a single-screen layout
        fits into the window's middle."""
        _, width, height = self._window()
        scale = min(width / TOP_SCREEN[0], height / TOP_SCREEN[1])
        self.click(((width - TOP_SCREEN[0] * scale) / 2 + x * scale) / width,
                   ((height - TOP_SCREEN[1] * scale) / 2 + y * scale) / height)

    def click(self, fx: float, fy: float) -> None:
        """A left click at fraction (fx, fy) of the app window."""
        window, width, height = self._window()
        x = round(width * fx)
        y = round(height * fy)
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
    session.key("name", "start")
    session.wait(3)
    session.shot("files")
    session.key("name", "a")
    session.wait(2)
    session.key("name", "a")
    session.wait(10)
    session.shot("house")
    session.key("stick", "left", 1.2)
    session.wait(1)
    session.shot("keys-moved-link")
    pad.stick(32767, 0, 1.2)
    session.wait(1)
    session.shot("pad-moved-link")


OOT3D_BOOT = ["wait:20", "start", "wait:3", "a", "wait:2", "a", "wait:8"]
OOT3D_AMMO = 20
# Inventory.upgrades with a Bomb Bag and a Bullet Bag and room for the sticks and nuts: using ammo
# clamps its count to the bag (decomp/oot3d/docs/item_buttons.md).
OOT3D_BAGS = 0x8 | 0x4000 | 0x40000 | 0x100000
# Inside the Deku Tree: a file saved in a dungeon loads there, and the Slingshot is usable.
OOT3D_DEKU_TREE = 0x0
# What the slots file holds beside the ocarina, and the songs a child learns.
OOT3D_ITEMS = ("STICK", "NUT", "BOMB", "SLINGSHOT")
OOT3D_SONGS = ("LULLABY", "EPONA", "SARIA", "SUN", "TIME", "STORMS")


def _harness_file(cfg: Config, name: str, script: list[str], out: Path) -> Path:
    """Runs `script` on the harness and returns the save folder of its user folder."""
    game = GameSet.named(cfg.repo, name)
    run_name = f"input-test-{name}"
    with harness.HarnessSession(cfg, game, run_name, out / "file-harness.log") as session:
        steps.run(session, script, out, games.layout(cfg.repo, name))
    low = game.title_id[8:].lower()
    return next((cfg.repo / "scratch" / "harness" / run_name).rglob(f"title/*/{low}/data/00000001"))


def _copy_file(made: Path, user: Path) -> None:
    """Puts a save folder made on the harness into a test's user folder."""
    target = next(user.rglob(f"title/*/{made.parent.parent.name}/data/00000001"))
    shutil.copytree(made, target, dirs_exist_ok=True)


def _oot3d_slots_file(cfg: Config, user: Path, out: Path) -> None:
    """File 1 inside the Deku Tree with sticks, nuts, bombs, a Slingshot, the ocarina and a
    child's six songs, written by the game's own save: the items and songs go into the
    inventory and autosave writes the file on a scene change."""
    d = symbols.load(cfg.repo, "oot3d")
    inventory = symbols.global_(d, "gSaveContext") + symbols.offset(d, "SaveContext.inventory")
    items = inventory + symbols.offset(d, "Inventory.items")
    script = list(OOT3D_BOOT)
    for name in OOT3D_ITEMS:
        slot = d.define(f"SLOT_{name}")
        script += [f"poke:{items + slot:#x},{d.define(f'ITEM_{name}'):02x}",
                   f"poke:{inventory + symbols.offset(d, 'Inventory.ammo') + slot:#x},{OOT3D_AMMO:02x}"]
    songs = sum(1 << d.define(f"QUEST_SONG_{name}") for name in OOT3D_SONGS)
    script += [f"poke:{items + d.define('SLOT_OCARINA'):#x},{d.define('ITEM_OCARINA_FAIRY'):02x}",
               f"poke:{inventory + symbols.offset(d, 'Inventory.questItems'):#x},{songs.to_bytes(4, 'little').hex()}",
               f"poke:{inventory + symbols.offset(d, 'Inventory.upgrades'):#x},{OOT3D_BAGS.to_bytes(4, 'little').hex()}",
               "wait:1", "option:autosave,1", f"warp:{OOT3D_DEKU_TREE:#x}", "wait:12"]
    _copy_file(_harness_file(cfg, "oot3d", script, out), user)


# A new file, out of the opening to South Clock Town's owl statue, past its first-visit cutscene.
MM3D_BOOT = ["wait:20", "start", "wait:4", "a", "wait:2", "a", "wait:2", "a", "wait:3", "start", "wait:2", "a",
             "wait:2", "a", "wait:3", "a", "wait:3", "a", "wait:8", "warp:0xD800", "wait:10", "warp:0xD890",
             "wait:10", "waitcs:60,a", "wait:3"]
# Up to the statue, "Check", then "Yes" to its save prompt.
MM3D_SAVE = ["stick:0,-32767,0.5", "wait:1", "a", "wait:3", "a", "wait:4"]
MM3D_AMMO = 20
# SaveData.upgrades of a new file with a Quiver and a Bomb Bag: using ammo clamps its count to the bag.
MM3D_BAGS = 0x00120000 | 0x1 | 0x8
# What the slots file holds beside the ocarina and an empty bottle.
MM3D_ITEMS = ("BOW", "BOMB")


def _mm3d_slots_file(cfg: Config, user: Path, out: Path) -> None:
    """File 1 at South Clock Town's owl statue with the ocarina, a bow, bombs, an empty bottle,
    the Bunny Hood and every song, written by the game's own save there."""
    d = symbols.load(cfg.repo, "mm3d")
    save = symbols.global_(d, "gCommonData") + symbols.offset(d, "CommonData.save")
    items = save + symbols.offset(d, "SaveData.items")
    script = list(MM3D_BOOT)
    for name in MM3D_ITEMS:
        slot, item = d.define(f"SLOT_{name}"), d.define(f"ITEM_{name}")
        script += [f"poke:{items + slot:#x},{item:02x}",
                   f"poke:{save + symbols.offset(d, 'SaveData.ammo') + item:#x},{MM3D_AMMO:02x}"]
    first = d.define("QUEST_SONG_FIRST")
    songs = sum(1 << bit for bit in range(first, first + d.define("QUEST_SONG_COUNT"))) | 1 << d.define("QUEST_SONG_LULLABY_INTRO")
    script += [f"poke:{items + d.define('SLOT_OCARINA'):#x},{d.define('ITEM_OCARINA'):02x}",
               f"poke:{items + d.define('BOTTLE_SLOTS_FIRST'):#x},{d.define('ITEM_BOTTLE'):02x}",
               f"poke:{items + d.define('SLOT_MASK_BUNNY'):#x},{d.define('ITEM_MASK_BUNNY'):02x}",
               f"poke:{save + symbols.offset(d, 'SaveData.questItems'):#x},{songs.to_bytes(4, 'little').hex()}",
               f"poke:{save + symbols.offset(d, 'SaveData.upgrades'):#x},{MM3D_BAGS.to_bytes(4, 'little').hex()}",
               "wait:1"]
    _copy_file(_harness_file(cfg, "mm3d", script + MM3D_SAVE, out), user)


def _oot3d_file_1(session: Session) -> None:
    """From the boot to File 1 in play, on the keyboard."""
    session.wait(20)
    session.key("name", "start")
    session.wait(3)
    session.key("name", "a")
    session.wait(2)
    session.key("name", "a")
    session.wait(10)


def _mm3d_file_1(session: Session) -> None:
    """From the boot to File 1 in play, on the keyboard."""
    session.wait(30)
    session.key("name", "start")
    session.wait(12)
    session.key("name", "a")
    session.wait(5)
    session.key("name", "a")
    session.wait(4)
    session.key("name", "a")
    session.wait(15)


def _chorded(pad: Gamepad, button: str, seconds: float = 0.15) -> None:
    """A pad button pressed with the chord held."""
    pad.hold("lb", True)
    time.sleep(0.3)
    pad.button(button, seconds)
    time.sleep(0.1)
    pad.hold("lb", False)


def _slots(session: Session, pad: Gamepad) -> None:
    """OoT3D's item slots from a real pad and keyboard, in a file with items, the ocarina and
    songs and no slot set but the ocarina's: Menu opens the item screen, where Y, RB, the
    D-pad's Right and LB with A each take the item under the cursor; in play each uses its
    item, and 1 on the keyboard uses Y's with the HUD on key caps. The D-pad's Down brings
    the ocarina out, RB its song list, where the D-pad's Up takes Epona's Song, and Up in play
    plays it."""
    _oot3d_file_1(session)
    session.shot("play")
    pad.button("start")
    session.wait(2)
    session.shot("item-screen")
    for take in (lambda: pad.button("y"), lambda: pad.button("rb"), lambda: pad.dpad("right"),
                 lambda: _chorded(pad, "a")):
        take()
        session.wait(0.5)
        pad.stick(32767, 0, 0.2)
        session.wait(0.5)
    session.shot("assigned")
    # A key that assigns nothing: the panel turns to key caps.
    session.key("stick", "up")
    session.wait(0.5)
    session.shot("assigned-keys")
    pad.button("start")
    session.wait(2)
    session.shot("play-slots")
    for name, use in (("y", lambda: pad.button("y")), ("rb", lambda: pad.button("rb")),
                      ("d-right", lambda: pad.dpad("right")), ("chord-a", lambda: _chorded(pad, "a"))):
        use()
        session.wait(1.5)
        session.shot(f"used-{name}")
        # The sword's button puts the item away.
        pad.button("x")
        session.wait(2.5)
    pad.hold("lb", True)
    session.wait(0.5)
    session.shot("chord-held")
    pad.hold("lb", False)
    session.key("slot", "0")
    session.wait(1.5)
    session.shot("used-key-1")
    session.mouse_button("sword")
    session.wait(2.5)
    pad.dpad("down")
    session.wait(3)
    session.shot("ocarina")
    pad.button("rb")
    session.wait(1.5)
    pad.stick(32767, 0, 0.2)
    session.wait(0.5)
    pad.dpad("up")
    session.wait(0.5)
    session.shot("song-list")
    pad.button("a")
    session.wait(2)
    pad.button("b")
    session.wait(2.5)
    session.shot("play-song-slot")
    pad.dpad("up")
    for shot in range(4):
        session.wait(2.5)
        session.shot(f"song-{shot}")


def _mm3d(session: Session, pad: Gamepad) -> None:
    """The overlay layout: keys reach file select, a tap on slot 2 makes a file, Start skips the opening."""
    session.wait(30)
    session.key("name", "start")
    session.wait(12)
    session.shot("title")
    session.key("name", "a")
    session.wait(5)
    session.shot("files")
    session.click(*MM3D_SLOT_2)
    session.wait(4)
    session.shot("tapped-slot-2")
    session.key("name", "a")
    session.wait(6)
    session.shot("narration")
    session.key("name", "start")
    session.wait(5)
    session.shot("skipped-to-woods")


# The ocarina screen's song list button on the touch screen, and where the overlay layout draws
# that screen on the top one (patch/mm3d/overlay.toml, layouts.ocarina).
MM3D_SONG_LIST = (290, 215)
MM3D_OCARINA_PIECE = (4, 4, 128, 96)
TOUCH_SCREEN = (320, 240)


def _slots_mm3d(session: Session, pad: Gamepad) -> None:
    """MM3D's item slots from a real pad and keyboard, in a file with the ocarina, a bow, bombs,
    a bottle and songs and no slot set but the ocarina's: Menu opens the Items screen, where
    Y takes the bow, RB and the D-pad's Right the bombs and LB with A the bottle; in play each
    uses its item, and 1 on the keyboard the bow with the HUD on key caps. The D-pad's Down
    brings the ocarina out, a click its song list, where the D-pad's Up takes the Song of Time;
    in play a tap of Up does nothing and Up held plays it."""
    _mm3d_file_1(session)
    session.shot("play")
    pad.button("start")
    session.wait(2)
    session.shot("items-screen")
    pad.button("y")
    session.wait(0.5)
    for _ in range(4):
        pad.stick(32767, 0, 0.2)
        session.wait(0.5)
    pad.button("rb")
    session.wait(0.5)
    pad.dpad("right")
    session.wait(0.5)
    for x, y in ((0, 32767), (0, 32767), (-32767, 0)):
        pad.stick(x, y, 0.2)
        session.wait(0.5)
    _chorded(pad, "a")
    session.wait(0.5)
    session.shot("assigned")
    session.key("stick", "up")
    session.wait(0.5)
    session.shot("assigned-keys")
    pad.button("start")
    session.wait(2)
    session.shot("play-slots")
    for name, use in (("y", lambda: pad.button("y", 1.0)), ("rb", lambda: pad.button("rb")),
                      ("d-right", lambda: pad.dpad("right")), ("chord-a", lambda: _chorded(pad, "a"))):
        use()
        session.wait(1.5)
        session.shot(f"used-{name}")
        pad.button("x")
        session.wait(2.5)
    pad.hold("lb", True)
    session.wait(0.5)
    session.shot("chord-held")
    pad.hold("lb", False)
    session.key("slot", "0", 1.0)
    session.wait(1.5)
    session.shot("used-key-1")
    session.mouse_button("sword")
    session.wait(2.5)
    pad.dpad("down")
    session.wait(3)
    session.shot("ocarina")
    piece = MM3D_OCARINA_PIECE
    session.click_top(piece[0] + MM3D_SONG_LIST[0] * piece[2] / TOUCH_SCREEN[0],
                      piece[1] + MM3D_SONG_LIST[1] * piece[3] / TOUCH_SCREEN[1])
    session.wait(1.5)
    pad.dpad("up")
    session.wait(0.5)
    session.shot("song-list")
    # The sword's button is the game's B there: it puts the ocarina away.
    pad.button("x")
    session.wait(2.5)
    session.shot("play-song-slot")
    pad.dpad("up")
    session.wait(3)
    session.shot("song-tapped")
    held = threading.Thread(target=pad.dpad, args=("up", 1.0))
    held.start()
    session.wait(0.3)
    session.shot("song-holding")
    held.join()
    for shot in range(6):
        session.wait(5)
        session.shot(f"song-{shot}")


# The Items screen's Masks tab on the touch screen, and where the overlay layout draws that
# screen on the top one (patch/mm3d/overlay.toml, layouts.menu).
MM3D_MASKS_TAB = (115, 222)
MM3D_MENU_PIECE = (59.2, 28.8, 281.6, 211.2)


def _masks_mm3d(session: Session, pad: Gamepad) -> None:
    """MM3D's Masks screen from a real pad: Menu opens Items, a click its Masks tab, the stick
    goes to the Bunny Hood and the D-pad's Left takes it; in play Left puts it on and takes it
    off. The right stick's click is first person and again leaves it."""
    _mm3d_file_1(session)
    pad.button("start")
    session.wait(2)
    piece = MM3D_MENU_PIECE
    session.click_top(piece[0] + MM3D_MASKS_TAB[0] * piece[2] / TOUCH_SCREEN[0],
                      piece[1] + MM3D_MASKS_TAB[1] * piece[3] / TOUCH_SCREEN[1])
    session.wait(2)
    session.shot("masks-screen")
    for x, y in ((32767, 0), (32767, 0), (0, 32767)):
        pad.stick(x, y, 0.2)
        session.wait(0.5)
    pad.dpad("left")
    session.wait(0.5)
    session.shot("assigned")
    pad.button("start")
    session.wait(2)
    session.shot("play")
    pad.dpad("left")
    session.wait(3)
    session.shot("mask-on")
    pad.dpad("left")
    session.wait(3)
    session.shot("mask-off")
    pad.button("rs")
    session.wait(1.5)
    session.shot("first-person")
    pad.button("rs")
    session.wait(1.5)
    session.shot("third-person")


def _verbs_mm3d(session: Session, pad: Gamepad) -> None:
    """MM3D's screens from play: M and the pad's View open the map, R brings the ocarina out,
    LB with Menu opens Gear; the sword's button leaves each."""
    _mm3d_file_1(session)
    for name, press in (("key-map", lambda: session.key("play", "map")), ("pad-view", lambda: pad.button("select")),
                        ("key-ocarina", lambda: session.key("play", "ocarina")),
                        ("pad-pause", lambda: _chorded(pad, "start")), ("key-pause", lambda: session.key("play", "pause"))):
        press()
        session.wait(3)
        session.shot(name)
        pad.button("b")
        session.wait(3)
    session.shot("play-again")


SETUP_GAME = "mm3d"
# The game whose base mod the downloads hold as an archive this setup opens (The One Screen's .tar.gz).
ARCHIVE_GAME = "oot3d"


def _without_set(cfg: Config, user: Path, out: Path, name: str = SETUP_GAME) -> None:
    """The test's user folder with a load/ of its own that lacks `name`'s set: the other
    games' folders are links to the installed ones."""
    load = user / "Azahar" / "load"
    load.unlink()
    missing = GameSet.named(cfg.repo, name).title_id
    for kind in ("mods", "textures"):
        (load / kind).mkdir(parents=True)
        for folder in (cfg.azahar_data / "load" / kind).iterdir():
            if folder.name != missing:
                (load / kind / folder.name).symlink_to(folder, target_is_directory=True)


def _setup(session: Session, pad: Gamepad) -> None:
    """A game whose set is missing: the picker says so, A runs the tools' installer on the
    player's dump with its stages on screen, and the game boots with the set it made."""
    session.expect("picker: on")
    session.wait(2)
    pad.stick(32767, 0, 0.3)
    session.expect("picker: on Majora")
    session.wait(1)
    session.shot("not-set-up")
    pad.button("a")
    session.expect("picker: setting up")
    session.wait(4)
    session.shot("setting-up")
    session.expect("the set is in place", 1200)
    session.expect("booting")
    session.wait(40)
    session.key("name", "start")
    session.wait(12)
    session.shot("booted")


AIM_MOVES = (("right-400", 400, 0), ("left-800", -800, 0), ("down-300", 400, 300), ("back", 0, -300))


def _aim_turns(session: Session) -> None:
    """Mouse aim once the look is first person: the app takes the mouse, each move turns the
    view by 0.05 degrees a count (app/src/mouse_aim.hpp), and the last one comes back to the start."""
    session.expect("mouse captured for aim")
    session.wait(1)
    session.shot("first-person")
    for name, dx, dy in AIM_MOVES:
        session.mouse(dx, dy)
        session.wait(1)
        session.shot(name)


def _aim_released(session: Session) -> None:
    """The app's own count of the mouse's travel while it held it is what the test sent."""
    travel = (sum(abs(dx) for _, dx, _ in AIM_MOVES), sum(abs(dy) for _, _, dy in AIM_MOVES))
    line = session.expect("mouse released")
    if not line.endswith(f"after {travel[0]} by {travel[1]} counts"):
        raise RuntimeError(f"the app counted another travel than {travel}: {line}")
    session.wait(1)
    session.shot("released")


def _aim(session: Session, pad: Gamepad) -> None:
    """OoT3D: a file saved inside the Deku Tree (Link's house has no first person), the
    first-person key, the mouse turns the view, and the sword's button gives the mouse back."""
    session.wait(20)
    session.key("name", "start")
    session.wait(3)
    session.key("name", "a")
    session.wait(2)
    session.key("name", "a")
    session.wait(10)
    session.shot("deku-tree")
    session.key("play", "first_person")
    session.wait(1)
    session.shot("first-person")
    _aim_turns(session)
    session.mouse_button("sword")
    _aim_released(session)


def _aim_mm3d(session: Session, pad: Gamepad) -> None:
    """MM3D: the file at South Clock Town's owl statue, the first-person key, the mouse turns
    the view, and the key again gives the mouse back."""
    session.wait(30)
    session.key("name", "start")
    session.wait(12)
    session.key("name", "a")
    session.wait(5)
    session.key("name", "a")
    session.wait(4)
    session.key("name", "a")
    session.wait(15)
    session.shot("statue")
    session.key("play", "first_person")
    session.wait(1)
    session.shot("first-person")
    _aim_turns(session)
    session.key("play", "first_person")
    _aim_released(session)


def _setup_refused(session: Session, pad: Gamepad) -> None:
    """A setup the installer refuses: the picker shows why, and A goes back to the tiles."""
    session.expect("picker: on")
    session.wait(2)
    pad.stick(32767, 0, 0.3)
    session.expect("picker: on Majora")
    pad.button("a")
    session.expect("installer ended with 1", 300)
    session.wait(1)
    session.shot("refused")
    pad.button("a")
    session.expect("picker: on Majora")
    session.wait(1)
    session.shot("back")


def _wrong_dump(cfg: Config, out: Path) -> Config:
    """SETUP_GAME's dump with nothing after its ExeFS and one byte of its code changed: the
    game's title and icon, not the revision the set is made for."""
    title_id = GameSet.named(cfg.repo, SETUP_GAME).title_id
    dump = ctr.open_cci(cfg.rom(title_id))
    with cfg.rom(title_id).open("rb") as stream:
        data = bytearray(stream.read(dump.offset + dump.exefs_offset + dump.exefs_size))
    data[dump.offset + dump.exefs_offset + 0x1200] ^= 0xFF
    wrong = out / "dumps" / f"{play.TITLES[SETUP_GAME]} (another revision).3ds"
    wrong.parent.mkdir(parents=True)
    wrong.write_bytes(data)
    # The core reads the icon only from an image of the whole size; the rest is a hole.
    os.truncate(wrong, cfg.rom(title_id).stat().st_size)
    return replace(cfg, roms=cfg.roms | {title_id: wrong})


def _base_mod(cfg: Config) -> Path:
    """The folder of SETUP_GAME's base mod among the downloads."""
    base = GameSet.named(cfg.repo, SETUP_GAME).base
    return next(p for p in cfg.mod_sources.rglob(base["code_patch"]) if p.is_file()).parent


def _missing_mod(cfg: Config, out: Path) -> Config:
    """A mods folder with nothing in it."""
    (out / "mods").mkdir(parents=True)
    return replace(cfg, mod_sources=out / "mods")


def _different_mod(cfg: Config, out: Path) -> Config:
    """A mods folder whose copy of SETUP_GAME's base mod has one file that is not the set's."""
    base = GameSet.named(cfg.repo, SETUP_GAME).base
    shutil.copytree(_base_mod(cfg), out / "mods" / "base")
    with (out / "mods" / "base" / base["exheader"]).open("ab") as stream:
        stream.write(b"changed")
    return replace(cfg, mod_sources=out / "mods")


def _without_archive_set(cfg: Config, user: Path, out: Path) -> None:
    _without_set(cfg, user, out, ARCHIVE_GAME)


def _archived_mod(cfg: Config, out: Path) -> Config:
    """A mods folder as a player has it after downloading: ARCHIVE_GAME's base mod still the
    archive its site gives, its texture pack unpacked. Both are links to the checkout's copies."""
    game = GameSet.named(cfg.repo, ARCHIVE_GAME)
    mods = out / "mods"
    mods.mkdir(parents=True)
    unpacked = source_check.find_files(cfg.mod_sources, game.base["name"], game.base["from"],
                                       game.base["code_patch"], game.base["sha256"])
    download = next(p for p in cfg.mod_sources.iterdir() if unpacked.is_relative_to(p))
    (archive,) = (p for p in download.iterdir() if p.is_file() and p.name.endswith(source_check.UNPACKED))
    os.link(archive, mods / archive.name)
    pack = source_check.find_folder(cfg.mod_sources, game.textures["name"], game.textures["from"],
                                    game.textures["folder"], game.textures["sha256"])
    shutil.copytree(pack, mods / "textures" / pack.name, copy_function=os.link)
    return replace(cfg, mod_sources=mods)


def _setup_archive(session: Session, pad: Gamepad) -> None:
    """A base mod left as its downloaded archive: the setup unpacks it and the game boots on it."""
    session.expect("picker: on Ocarina")
    session.wait(2)
    session.shot("not-set-up")
    pad.button("a")
    session.expect("Unpacking", 60)
    session.expect("the set is in place", 1200)
    session.expect("booting")
    session.wait(30)
    session.key("name", "start")
    session.wait(10)
    session.shot("booted")


# The home folder the first run's browser opens in (_first_run_home), by row.
FIRST_RUN_FOLDERS = ("Empty", "Mods", "ROM")


def _first_run_home(cfg: Config, out: Path) -> Path:
    """A home folder for the first run: an empty folder, the mod downloads and ROM/3DS with the
    player's dumps beside a file that is none."""
    home = out / "home"
    (home / "Empty").mkdir(parents=True)
    (home / "Mods").symlink_to(cfg.mod_sources, target_is_directory=True)
    dumps = home / "ROM" / "3DS"
    dumps.mkdir(parents=True)
    for rom in cfg.roms.values():
        (dumps / rom.name).symlink_to(rom)
    (dumps / "notes.txt").write_text("not a dump\n")
    return home


def _browse_to(session: Session, pad: Gamepad, *names: str) -> None:
    """Walks the folder browser, just opened, down into each of `names` with the pad: the stick
    down to its row, then A."""
    for name in names:
        session.expect("folders: row Use this folder")
        for _ in range(64):
            pad.stick(0, 32767, 0.12)
            # Long enough centred for the app to see the stick let go.
            session.wait(0.1)
            if f"folders: row {name}" in session.expect("folders: row"):
                break
        pad.button("a")
        session.expect(f"/{name} (")


def _first_run(session: Session, pad: Gamepad) -> None:
    """A package started with nothing known: the window asks for the dump folder and the mods
    folder, each chosen in its folder browser, and the picker opens over the dumps found. A
    folder without a dump is refused on the screen. The options then ask for both again."""
    session.expect("folders: asking --roms")
    session.wait(2)
    session.shot("ask-roms")
    pad.button("a")
    session.expect("folders: in")
    session.wait(1)
    session.shot("browser-home")
    _browse_to(session, pad, "Empty")
    pad.button("x")
    session.expect("folders: refused --roms")
    session.wait(1)
    session.shot("roms-refused")
    pad.button("a")
    session.expect("folders: in")
    _browse_to(session, pad, "ROM", "3DS")
    session.wait(1)
    session.shot("browser-dumps")
    pad.button("a")
    session.expect("folders: asking --mods")
    session.wait(2)
    session.shot("ask-mods")
    session.press([SETUP_UI_CONFIRM])
    session.expect("folders: in")
    _browse_to(session, pad, "Mods")
    session.wait(1)
    session.shot("browser-mods")
    # The first row, "Use this folder", with the mouse.
    session.click(0.5, 0.255)
    session.expect("folders: took --mods")
    session.expect("picker: on")
    session.wait(2)
    session.shot("picker")
    pad.stick(32767, 0, 0.3)
    session.expect("picker: on Majora")
    session.wait(1)
    session.shot("picker-second")
    # The options' last row asks again: the dump folder is kept, the mods folder changed.
    pad.button("x")
    row = session.expect("options: row ")
    while not row.endswith("options: row folders"):
        pad.stick(0, 32767, 0.12)
        session.wait(0.1)
        row = session.expect("options: row ")
    session.wait(1)
    session.shot("options-folders")
    pad.button("a")
    session.expect("folders: asking --roms")
    session.wait(2)
    session.shot("ask-roms-again")
    pad.button("b")
    session.expect("folders: kept --roms")
    session.expect("folders: asking --mods")
    session.wait(1)
    pad.button("a")
    session.expect("folders: in")
    _browse_to(session, pad, "Empty")
    pad.button("x")
    session.expect("folders: took --mods")
    session.expect("picker: on")
    session.wait(2)
    session.shot("picker-after-folders")


def _older_desktop(session: Session, pad: Gamepad) -> None:
    """The AppImage on a desktop of the oldest distribution it runs on (desktop_container.py),
    told the dump folder: it unpacks, starts and opens the picker over the dumps."""
    session.expect("picker: on Ocarina", 180)
    session.wait(2)
    session.shot("picker")
    session.key("menu", "right")
    session.expect("picker: on Majora")
    session.wait(1)
    session.shot("picker-second")


def _menu_pad(session: Session, pad: Gamepad) -> None:
    """The menus read the pad by its own button names whatever game was played last: after
    OoT3D, whose set trades X and Y on their way to the game, X still opens the options and
    closes them, and Y does neither."""
    session.expect("picker: on Ocarina")
    session.wait(2)
    pad.button("a")
    session.expect("booting")
    session.wait(15)
    pad.button("guide")
    session.expect("picker: on Ocarina")
    session.wait(2)
    pad.button("y")
    session.quiet("options: open", 2)
    pad.button("x")
    session.expect("options: open", 5)
    session.wait(1)
    session.shot("options-by-x")
    pad.button("y")
    session.quiet("options: closed", 2)
    pad.button("x")
    session.expect("options: closed", 5)
    session.wait(1)
    session.shot("picker-by-x")


def _hotswap(session: Session, pad: Gamepad) -> None:
    """MM3D's mods swapped while it runs: F7 turns the textures off and on, F6 takes the patch out
    and puts it back, F5 reads it again; the game runs on (the log names each swap)."""
    _mm3d(session, pad)
    session.wait(25)
    session.shot("woods")
    for key in ("toggle_textures", "toggle_textures", "toggle_patch", "toggle_patch", "reload_patch"):
        session.key("window", key)
        session.wait(3)
        session.shot(key)


def _picker(session: Session, pad: Gamepad) -> None:
    """The shortcut's picker: keys boot OoT3D and the menu key returns; the pad boots MM3D and Guide
    returns; a click boots OoT3D again."""
    session.wait(8)
    session.shot("picker")
    session.key("menu", "a")
    session.wait(20)
    session.shot("oot3d")
    session.key("window", "menu")
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
    game = session.expect("picker: on ")
    while "Majora" not in game:
        session.key("menu", "right")
        game = session.expect("picker: on ")
    session.key("menu", "x")
    row = session.expect("options: row ")
    session.shot("options")
    while not row.endswith("options: row cutscene_skip"):
        session.key("menu", "down")
        row = session.expect("options: row ")
    session.key("menu", "a")
    session.expect("options: cutscene_skip = Off")
    session.shot("skip-off")
    session.key("menu", "b")
    session.expect("picker: on ")
    session.shot("picker")
    session.key("menu", "a")
    session.wait(30)
    session.key("name", "start")
    session.wait(12)
    session.key("name", "a")
    session.wait(5)
    session.click(*MM3D_SLOT_2)
    session.wait(4)
    session.key("name", "a")
    session.wait(6)
    session.shot("narration")
    session.key("name", "start")
    session.wait(5)
    session.shot("narration-after-start")


def _options_oot3d(session: Session, pad: Gamepad) -> None:
    """The picker's options for OoT3D: X opens them, Down and A turn Quick Text off, and File 1
    then boots with the option written. Each step waits on the menu's own log line."""
    game = session.expect("picker: on ")
    while "Ocarina" not in game:
        session.key("menu", "right")
        game = session.expect("picker: on ")
    session.key("menu", "x")
    row = session.expect("options: row ")
    session.shot("options")
    while not row.endswith("options: row quick_text"):
        session.key("menu", "down")
        row = session.expect("options: row ")
    session.key("menu", "a")
    session.expect("options: quick_text = Off")
    session.shot("quick-text-off")
    session.key("menu", "b")
    session.expect("picker: on ")
    session.key("menu", "a")
    session.wait(20)
    session.key("name", "start")
    session.wait(3)
    session.key("name", "a")
    session.wait(2)
    session.key("name", "a")
    session.wait(10)
    session.shot("house")


# Direct launches of one game, and the installed shortcut's picker.
DIRECT = {"oot3d": _oot3d, "mm3d": _mm3d, "verbs-mm3d": _verbs_mm3d, "masks-mm3d": _masks_mm3d, "hotswap": _hotswap, "slots": _slots, "slots-small": _slots,
          "slots-mm3d": _slots_mm3d, "slots-mm3d-small": _slots_mm3d, "aim": _aim, "aim-mm3d": _aim_mm3d}
# The game a direct scenario launches, when it is not the scenario's name.
DIRECT_GAME = {"hotswap": "mm3d", "slots": "oot3d", "slots-small": "oot3d", "slots-mm3d": "mm3d", "slots-mm3d-small": "mm3d",
               "aim": "oot3d", "aim-mm3d": "mm3d", "verbs-mm3d": "mm3d", "masks-mm3d": "mm3d"}
# The window of a scenario that is not WIDTH by HEIGHT.
SIZES = {"slots-small": SMALL, "slots-mm3d-small": SMALL}
# What a scenario puts in its user folder before the app starts.
PREPARE = {"slots": _oot3d_slots_file, "slots-small": _oot3d_slots_file, "slots-mm3d": _mm3d_slots_file,
           "slots-mm3d-small": _mm3d_slots_file, "aim": _oot3d_slots_file, "aim-mm3d": _mm3d_slots_file, "verbs-mm3d": _mm3d_slots_file, "masks-mm3d": _mm3d_slots_file,
           "setup": _without_set,
           "setup-wrong-dump": _without_set, "setup-missing-mod": _without_set, "setup-different-mod": _without_set,
           "setup-archive": _without_archive_set}
# Scenarios on the picker as `z3dmods play` starts it, on the test's own user folder.
PICKER = {"menu-pad": _menu_pad, "setup": _setup, "setup-wrong-dump": _setup_refused, "setup-missing-mod": _setup_refused,
          "setup-different-mod": _setup_refused, "setup-archive": _setup_archive}
# The player's files a picker scenario runs on, when they are not the config's.
FILES = {"setup-wrong-dump": _wrong_dump, "setup-missing-mod": _missing_mod, "setup-different-mod": _different_mod,
         "setup-archive": _archived_mod}
# Scenarios on the package `z3dmods package` built, started with nothing known: by the folder's
# launcher, and as the AppImage.
PACKAGE = {"first-run": _first_run, "first-run-appimage": _first_run, "appimage-older-desktop": _older_desktop}
PACKAGE_PROGRAM = {"first-run": lambda cfg: package.folder(cfg) / "z3d",
                   "first-run-appimage": lambda cfg: package.folder(cfg).with_name(package.APPIMAGE),
                   "appimage-older-desktop": lambda cfg: package.folder(cfg).with_name(package.APPIMAGE)}
# The package scenarios that run in the older desktop's container.
OLDER_DESKTOP = ("appimage-older-desktop",)
SCENARIOS = {**DIRECT, **PICKER, **PACKAGE, "picker": _picker, "options": _options, "options-oot3d": _options_oot3d}


def _session(cfg: Config, scenario: str, out: Path) -> Session:
    user = out / "user"
    keys = AppKeys(cfg)
    if scenario in PACKAGE:
        launcher = PACKAGE_PROGRAM[scenario](cfg)
        if not launcher.exists():
            raise RuntimeError(f"{launcher} is missing; run z3dmods package")
        user.mkdir(parents=True)
        home = _first_run_home(cfg, out)
        if scenario in OLDER_DESKTOP:
            # The dumps are links: the container sees their folders too.
            dumps = sorted({rom.parent for rom in cfg.roms.values()})
            program = [str(launcher), "--roms", str(home / "ROM" / "3DS"), "--mods", str(home / "Empty")]
            return Session(out, desktop_container.command(cfg, out, [launcher, *dumps],
                                                          {config.APP_DATA_ENV: str(user)}, program), keys,
                           program=desktop_container.PROGRAM)
        return Session(out, [str(launcher)], keys, {config.APP_DATA_ENV: str(user), "HOME": str(home)})
    app.seed_user(cfg, user, fresh=True)
    if scenario in PREPARE:
        PREPARE[scenario](cfg, user, out)
    if scenario in DIRECT:
        return Session(out, play.launch_args(cfg, DIRECT_GAME.get(scenario, scenario), user), keys,
                       size=SIZES.get(scenario, (WIDTH, HEIGHT)))
    if scenario in PICKER:
        files = FILES[scenario](cfg, out) if scenario in FILES else cfg
        # The picker's tools are a program of their own: they are told the test's downloads.
        return Session(out, play.launch_args(files, None, user), keys, {config.MOD_SOURCES_ENV: str(files.mod_sources)})
    entry = Path.home() / ".local/share/applications" / f"{play.DESKTOP_ID}.desktop"
    if not entry.exists():
        raise RuntimeError(f"{entry} is missing; run z3dmods shortcut")
    return Session(out, shlex.split(shortcut.desktop_exec(entry)), keys, {config.APP_DATA_ENV: str(user)})


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
