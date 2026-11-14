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

from z3dmods import app, config, games, harness, play, shortcut, steps, symbols
from z3dmods.config import Config
from z3dmods.install import GameSet

WIDTH, HEIGHT = 1280, 720
# The player's Azahar keys (pad_input.cpp kKeys), as X keysyms.
KEYS = {
    "a": "a", "b": "s", "x": "z", "y": "x", "start": "m", "select": "n", "l": "q", "r": "w",
    "zl": "e", "dpad_left": "f",
    "menu": "Escape", "reload_patch": "F5", "toggle_patch": "F6", "toggle_textures": "F7", "stick_up": "Up", "stick_down": "Down", "stick_left": "Left", "stick_right": "Right",
}
PAD_BUTTONS = {"a": ecodes.BTN_SOUTH, "b": ecodes.BTN_EAST, "x": ecodes.BTN_NORTH, "y": ecodes.BTN_WEST,
               "lb": ecodes.BTN_TL, "rb": ecodes.BTN_TR,
               "start": ecodes.BTN_START, "select": ecodes.BTN_SELECT, "guide": ecodes.BTN_MODE}
# File select's second slot on MM3D's touch screen, which the overlay layout puts beside the top
# screen there: the right two fifths of the window, half its height, centred.
MM3D_SLOT_2 = (0.6 + 0.4 * 0.51, 0.25 + 0.5 * 0.54)
AXIS = AbsInfo(value=0, min=-32768, max=32767, fuzz=16, flat=128, resolution=0)


class Gamepad:
    """A uinput gamepad with standard evdev codes, which SDL maps like a common pad."""

    def __init__(self) -> None:
        self._device = UInput(
            {ecodes.EV_KEY: list(PAD_BUTTONS.values()),
             ecodes.EV_ABS: [(ecodes.ABS_X, AXIS), (ecodes.ABS_Y, AXIS), (ecodes.ABS_RX, AXIS), (ecodes.ABS_RY, AXIS)]},
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
        self._log_read = 0

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

    def key(self, button: str, seconds: float = 0.15) -> None:
        keysym = KEYS[button]
        subprocess.run(["xdotool", "keydown", keysym], env=self._env, check=True)
        time.sleep(seconds)
        subprocess.run(["xdotool", "keyup", keysym], env=self._env, check=True)

    def mouse(self, dx: int, dy: int) -> None:
        """The mouse moved by (dx, dy) counts, x right and y down."""
        subprocess.run(["xdotool", "mousemove_relative", "--", str(dx), str(dy)], env=self._env, check=True)

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


OOT3D_BOOT = ["wait:20", "start", "wait:3", "a", "wait:2", "a", "wait:8"]
# chord_items.cpp's Store in the save's unused bytes: the marker, then the child's away page,
# a slot for each of the game's Y, X, I and II and the grid node each waits in.
OOT3D_STORE = "c4ff{x:02x}ff{ii:02x}00000000"
OOT3D_AMMO = 30
# Inside the Deku Tree: a file saved in a dungeon loads there, and the Slingshot is usable.
OOT3D_DEKU_TREE = 0x0


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


def _oot3d_chord_file(cfg: Config, user: Path, out: Path) -> None:
    """File 1 with a Slingshot on X and a Deku Stick on II of the child's second page, written
    by the game's own save: the items go into the inventory, the game puts them in the item
    screen's grid, the page's bytes name them, and autosave writes the file on a scene change."""
    d = symbols.load(cfg.repo, "oot3d")
    save = symbols.global_(d, "gSaveContext")
    inventory = save + symbols.offset(d, "SaveContext.inventory")
    items = {name: (d.define(f"SLOT_{name}"), d.define(f"ITEM_{name}")) for name in ("SLINGSHOT", "STICK")}
    script = list(OOT3D_BOOT)
    for slot, item in items.values():
        script += [f"poke:{inventory + symbols.offset(d, 'Inventory.items') + slot:#x},{item:02x}",
                   f"poke:{inventory + symbols.offset(d, 'Inventory.ammo') + slot:#x},{OOT3D_AMMO:02x}"]
    store = OOT3D_STORE.format(x=items["SLINGSHOT"][0], ii=items["STICK"][0])
    script += ["wait:1", f"poke:{save + symbols.offset(d, 'SaveContext.unused_6C'):#x},{store}",
               "option:autosave,1", f"warp:{OOT3D_DEKU_TREE:#x}", "wait:12"]
    _copy_file(_harness_file(cfg, "oot3d", script, out), user)


# A new file, out of the opening to South Clock Town's owl statue, past its first-visit cutscene.
MM3D_BOOT = ["wait:20", "start", "wait:4", "a", "wait:2", "a", "wait:2", "a", "wait:3", "start", "wait:2", "a",
             "wait:2", "a", "wait:3", "a", "wait:3", "a", "wait:8", "warp:0xD800", "wait:10", "warp:0xD890",
             "wait:10", "waitcs:60,a", "wait:3"]
# Up to the statue, "Check", then "Yes" to its save prompt.
MM3D_SAVE = ["stick:0,-32767,0.5", "wait:1", "a", "wait:3", "a", "wait:4"]
MM3D_AMMO = 30
# The equipment row whose slot bytes hold the page that is away (patch/mm3d/chord_items.cpp).
MM3D_AWAY_ROW = 1


def _mm3d_chord_file(cfg: Config, user: Path, out: Path) -> None:
    """File 1 at South Clock Town's owl statue with bombs on X and a bow on II of the second
    page, written by the game's own save there."""
    d = symbols.load(cfg.repo, "mm3d")
    save = symbols.global_(d, "gCommonData") + symbols.offset(d, "CommonData.save")
    away = (save + symbols.offset(d, "SaveData.equipment") + symbols.offset(d, "EquipmentData.buttonSlots")
            + MM3D_AWAY_ROW * (d.define("EQUIP_BUTTON_II") + 1))
    script = list(MM3D_BOOT)
    for name, button in (("BOMB", "X"), ("BOW", "II")):
        slot, item = d.define(f"SLOT_{name}"), d.define(f"ITEM_{name}")
        script += [f"poke:{save + symbols.offset(d, 'SaveData.items') + slot:#x},{item:02x}",
                   f"poke:{save + symbols.offset(d, 'SaveData.ammo') + item:#x},{MM3D_AMMO:02x}",
                   f"poke:{away + d.define(f'EQUIP_BUTTON_{button}'):#x},{slot:02x}"]
    _copy_file(_harness_file(cfg, "mm3d", script + ["wait:1"] + MM3D_SAVE, out), user)


def _chord(session: Session, pad: Gamepad) -> None:
    """The second item page from a real pad, in a file with a Slingshot on its X and a Deku
    Stick on its II: with LB held the HUD shows the page, the pad's X, held, aims the Slingshot
    and the pad's B takes the stick in hand; released, the first page is back."""
    session.wait(20)
    session.key("start")
    session.wait(3)
    session.key("a")
    session.wait(2)
    session.key("a")
    session.wait(10)
    session.shot("page-1")
    pad.hold("lb", True)
    session.wait(1)
    session.shot("chord-page")
    pad.hold("x", True)
    session.wait(2)
    session.shot("chord-x-aims")
    pad.hold("x", False)
    pad.hold("lb", False)
    session.wait(1)
    pad.button("b")
    session.wait(2)
    session.shot("released")
    pad.hold("lb", True)
    session.wait(1)
    pad.button("b")
    session.wait(2)
    session.shot("chord-b-stick")
    pad.hold("lb", False)
    session.wait(1)


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


def _chord_mm3d(session: Session, pad: Gamepad) -> None:
    """The second item page from a real pad in MM3D, in a file with bombs on its X and a bow on
    its II: with LB held the HUD shows the page, the pad's B, held, aims the bow and the pad's X
    takes out a bomb; released, the first page is back."""
    session.wait(30)
    session.key("start")
    session.wait(12)
    session.key("a")
    session.wait(5)
    session.key("a")
    session.wait(4)
    session.key("a")
    session.wait(15)
    session.shot("page-1")
    pad.hold("lb", True)
    session.wait(1)
    session.shot("chord-page")
    pad.hold("b", True)
    session.wait(2)
    session.shot("chord-b-aims")
    pad.hold("b", False)
    pad.hold("lb", False)
    session.wait(1)
    pad.button("b")
    session.wait(2)
    session.shot("released")
    pad.hold("lb", True)
    session.wait(1)
    pad.button("x")
    session.wait(1.5)
    session.shot("chord-x-bomb")
    pad.hold("lb", False)
    session.wait(1)


SETUP_GAME = "mm3d"


def _without_set(cfg: Config, user: Path, out: Path) -> None:
    """The test's user folder with a load/ of its own that lacks SETUP_GAME's set: the other
    games' folders are links to the installed ones."""
    load = user / "Azahar" / "load"
    load.unlink()
    missing = GameSet.named(cfg.repo, SETUP_GAME).title_id
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
    session.key("start")
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
    """OoT3D: a file saved inside the Deku Tree (Link's house has no first person), D-pad Left
    for first person, the mouse turns the view, and B gives the mouse back."""
    session.wait(20)
    session.key("start")
    session.wait(3)
    session.key("a")
    session.wait(2)
    session.key("a")
    session.wait(10)
    session.shot("deku-tree")
    session.key("dpad_left")
    session.wait(1)
    session.shot("d-pad-left")
    _aim_turns(session)
    session.key("b")
    _aim_released(session)


def _aim_mm3d(session: Session, pad: Gamepad) -> None:
    """MM3D: the file at South Clock Town's owl statue, ZL for first person, the mouse turns the
    view, and ZL again gives the mouse back."""
    session.wait(30)
    session.key("start")
    session.wait(12)
    session.key("a")
    session.wait(5)
    session.key("a")
    session.wait(4)
    session.key("a")
    session.wait(15)
    session.shot("statue")
    session.key("zl")
    session.wait(1)
    session.shot("zl")
    _aim_turns(session)
    session.key("zl")
    _aim_released(session)


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
    game = session.expect("picker: on ")
    while "Majora" not in game:
        session.key("stick_right")
        game = session.expect("picker: on ")
    session.key("x")
    row = session.expect("options: row ")
    session.shot("options")
    while not row.endswith("options: row cutscene_skip"):
        session.key("stick_down")
        row = session.expect("options: row ")
    session.key("a")
    session.expect("options: cutscene_skip = Off")
    session.shot("skip-off")
    session.key("b")
    session.expect("picker: on ")
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
    """The picker's options for OoT3D: X opens them, Down and A turn Quick Text off, and File 1
    then boots with the option written. Each step waits on the menu's own log line."""
    game = session.expect("picker: on ")
    while "Ocarina" not in game:
        session.key("stick_right")
        game = session.expect("picker: on ")
    session.key("x")
    row = session.expect("options: row ")
    session.shot("options")
    while not row.endswith("options: row quick_text"):
        session.key("stick_down")
        row = session.expect("options: row ")
    session.key("a")
    session.expect("options: quick_text = Off")
    session.shot("quick-text-off")
    session.key("b")
    session.expect("picker: on ")
    session.key("a")
    session.wait(20)
    session.key("start")
    session.wait(3)
    session.key("a")
    session.wait(2)
    session.key("a")
    session.wait(10)
    session.shot("house")


# Direct launches of one game, and the installed shortcut's picker.
DIRECT = {"oot3d": _oot3d, "mm3d": _mm3d, "hotswap": _hotswap, "chord": _chord, "chord-mm3d": _chord_mm3d,
          "aim": _aim, "aim-mm3d": _aim_mm3d}
# The game a direct scenario launches, when it is not the scenario's name.
DIRECT_GAME = {"hotswap": "mm3d", "chord": "oot3d", "chord-mm3d": "mm3d", "aim": "oot3d", "aim-mm3d": "mm3d"}
# What a scenario puts in its user folder before the app starts.
PREPARE = {"chord": _oot3d_chord_file, "chord-mm3d": _mm3d_chord_file, "aim": _oot3d_chord_file,
           "aim-mm3d": _mm3d_chord_file, "setup": _without_set}
# Scenarios on the picker as `z3dmods play` starts it, on the test's own user folder.
PICKER = {"menu-pad": _menu_pad, "setup": _setup}
SCENARIOS = {**DIRECT, **PICKER, "picker": _picker, "options": _options, "options-oot3d": _options_oot3d}


def _session(cfg: Config, scenario: str, out: Path) -> Session:
    user = out / "user"
    app.seed_user(cfg, user, fresh=True)
    if scenario in PREPARE:
        PREPARE[scenario](cfg, user, out)
    if scenario in DIRECT:
        return Session(out, play.launch_args(cfg, DIRECT_GAME.get(scenario, scenario), user))
    if scenario in PICKER:
        return Session(out, play.launch_args(cfg, None, user))
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
