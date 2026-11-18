"""Prove the windowed app's input path with real platform events, away from the player's desktop.

The app runs as `z3dmods play` runs it, on a fresh copy of the player's data, inside a headless
Gamescope session; the picker scenario runs the installed desktop shortcut's Exec line itself. Keys and the mouse go in as XTest events on that session's X server, the
gamepad is an SDL virtual pad inside the app alone, made and driven over its loopback channel
(app/src/test_pad.hpp), and frames come from Gamescope's screenshots. Output: scratch/input-test/<game>/.
"""

from __future__ import annotations

import json
import os
import shlex
import shutil
import signal
import subprocess
import threading
import time
import urllib.parse
import urllib.request
from collections.abc import Callable
from dataclasses import replace
from pathlib import Path

from z3dmods import (
    app,
    config,
    ctr,
    desktop_container,
    games,
    harness,
    options,
    package,
    play,
    shortcut,
    source_check,
    steps,
    symbols,
)
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
# Every button of an Xbox 360 pad by SDL's gamepad names, which the app's test pad takes.
PAD_BUTTONS = {"a": "a", "b": "b", "x": "x", "y": "y", "lb": "leftshoulder", "rb": "rightshoulder",
               "start": "start", "select": "back", "guide": "guide", "ls": "leftstick", "rs": "rightstick"}
PAD_DPAD = {"up": "dpup", "down": "dpdown", "left": "dpleft", "right": "dpright"}
# File select's second slot on MM3D's touch screen, which the overlay layout puts beside the top
# screen there: the right two fifths of the window, half its height, centred.
MM3D_SLOT_2 = (0.6 + 0.4 * 0.51, 0.25 + 0.5 * 0.54)


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
        env[config.TEST_PAD_ENV] = "1"
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

    def logged(self, text: str, seconds: float, start: int = 0) -> tuple[str, int]:
        """Waits for a line holding `text` from byte `start` of the log on, apart from expect's
        place in it; returns the line and the byte after it."""
        assert self._gamescope
        log = self.out / "gamescope.log"
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            data = log.read_bytes()
            at = data.find(text.encode(), start)
            if at >= 0 and (end := data.find(b"\n", at)) >= 0:
                return data[data.rfind(b"\n", 0, at) + 1:end].decode(errors="replace"), end + 1
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


class Gamepad:
    """The app's test pad (app/src/test_pad.hpp), attached on first use: only the app sees it."""

    def __init__(self, session: Session) -> None:
        self._session = session
        self._url = ""

    def _post(self, command: str, **query: str | int) -> None:
        if not self._url:
            line, _ = self._session.logged("test pad: listening on port", 60)
            self._url = f"http://127.0.0.1:{int(line.rsplit(' ', 1)[1])}/"
            self._send("attach")
            _, attached = self._session.logged("test pad: attached", 10)
            self._session.logged("[pad] opened", 10, attached)
        self._send(command, **query)

    def _send(self, command: str, **query: str | int) -> None:
        url = self._url + command + ("?" + urllib.parse.urlencode(query) if query else "")
        with urllib.request.urlopen(urllib.request.Request(url, method="POST"), timeout=10) as reply:
            reply.read()

    def hold(self, name: str, down: bool) -> None:
        self._post("button", name=PAD_BUTTONS[name], down=int(down))

    def button(self, name: str, seconds: float = 0.15) -> None:
        self.hold(name, True)
        time.sleep(seconds)
        self.hold(name, False)

    def dpad(self, direction: str, seconds: float = 0.15) -> None:
        self._post("button", name=PAD_DPAD[direction], down=1)
        time.sleep(seconds)
        self._post("button", name=PAD_DPAD[direction], down=0)

    def steer(self, x: int, y: int) -> None:
        """Left stick, -32768..32767 with y growing downward, left there."""
        self._post("axis", name="leftx", value=x)
        self._post("axis", name="lefty", value=y)

    def stick(self, x: int, y: int, seconds: float) -> None:
        """Left stick, -32768..32767 with y growing downward, held for `seconds`, then centred."""
        for value in ((x, y), (0, 0)):
            self._post("axis", name="leftx", value=value[0])
            self._post("axis", name="lefty", value=value[1])
            if value != (0, 0):
                time.sleep(seconds)


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
# The Kokiri Sword, Tunic and Boots, as worn (ItemEquips.equipment) and as owned (Inventory.equipment).
OOT3D_EQUIPMENT = 0x1101
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


def _oot3d_slots_file(cfg: Config, user: Path, out: Path, aimed_on_y: bool = False,
                      master_quest: bool = False) -> None:
    """File 1 inside the Deku Tree with sticks, nuts, bombs, a Slingshot, the ocarina and a
    child's six songs, written by the game's own save: the items and songs go into the
    inventory and autosave writes the file on a scene change. With `aimed_on_y` the Slingshot is
    on the game's Y and so on slot 1 (the patch's slots are made again from the game's buttons),
    and the Kokiri Sword on B. With `master_quest` the file is a Master Quest one."""
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
               f"poke:{inventory + symbols.offset(d, 'Inventory.upgrades'):#x},{OOT3D_BAGS.to_bytes(4, 'little').hex()}"]
    if aimed_on_y:
        save = symbols.global_(d, "gSaveContext")
        layout = save + symbols.offset(d, "SaveContext.itemMenuChild")
        equips = save + symbols.offset(d, "SaveContext.equips")
        script += [f"poke:{layout + d.define('ITEM_MENU_NODE_Y'):#x},{d.define('SLOT_SLINGSHOT'):02x}",
                   f"poke:{save + symbols.offset(d, 'SaveContext.unused_6C'):#x},00",
                   f"poke:{equips + symbols.offset(d, 'ItemEquips.buttonItems'):#x},{d.define('ITEM_SWORD_KOKIRI'):02x}",
                   f"poke:{equips + symbols.offset(d, 'ItemEquips.equipment'):#x},{OOT3D_EQUIPMENT.to_bytes(2, 'little').hex()}",
                   f"poke:{inventory + symbols.offset(d, 'Inventory.equipment'):#x},{OOT3D_EQUIPMENT.to_bytes(2, 'little').hex()}"]
    if master_quest:
        script.append(f"poke:{symbols.global_(d, 'gSaveContext') + symbols.offset(d, 'SaveContext.masterQuestFlag'):#x},01")
    script += ["wait:1", "option:autosave,1", f"warp:{OOT3D_DEKU_TREE:#x}", "wait:12"]
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


def _mm3d_slots_file(cfg: Config, user: Path, out: Path, notebook: bool = False, maps: bool = False,
                     aimed_on_y: bool = False) -> None:
    """File 1 at South Clock Town's owl statue with the ocarina, a bow, bombs, an empty bottle,
    the Bunny Hood, every song, with `notebook` the Bombers' Notebook, with `maps` every
    region's and scene's map and with `aimed_on_y` the bow on the game's Y and so on slot 1 (the patch's
    slots are made again from the game's buttons), written by the game's own save there."""
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
    songs |= notebook << d.define("QUEST_BOMBERS_NOTEBOOK")
    script += [f"poke:{items + d.define('SLOT_OCARINA'):#x},{d.define('ITEM_OCARINA'):02x}",
               f"poke:{items + d.define('BOTTLE_SLOTS_FIRST'):#x},{d.define('ITEM_BOTTLE'):02x}",
               f"poke:{items + d.define('SLOT_MASK_BUNNY'):#x},{d.define('ITEM_MASK_BUNNY'):02x}",
               f"poke:{save + symbols.offset(d, 'SaveData.questItems'):#x},{songs.to_bytes(4, 'little').hex()}",
               f"poke:{save + symbols.offset(d, 'SaveData.upgrades'):#x},{MM3D_BAGS.to_bytes(4, 'little').hex()}",
               "wait:1"]
    if maps:
        regions = save + symbols.offset(d, 'SaveData.weekEventReg') + d.define('WEEK_EVENT_REGION_MAPS')
        script.insert(-1, f"poke:{regions:#x},{(1 << d.define('REGION_MAP_COUNT')) - 1:02x}")
        # Every scene's map, as Tingle's maps show them: the minimap in play.
        scenes = save + symbols.offset(d, 'SaveData.scenesVisible')
        script.insert(-1, f"poke:{scenes:#x},{'ff' * 4 * d.define('SCENES_VISIBLE_WORDS')}")
    if aimed_on_y:
        equipment = save + symbols.offset(d, "SaveData.equipment")
        button_slots = equipment + symbols.offset(d, "EquipmentData.buttonSlots")
        y = d.define("EQUIP_BUTTON_Y")
        row = d.define("EQUIP_BUTTON_II") + 1
        # Row 1's Y holds the patch's marker: without it the slots are made again.
        script[-1:-1] = [f"poke:{equipment + symbols.offset(d, 'EquipmentData.buttonItems') + y:#x},{d.define('ITEM_BOW'):02x}",
                         f"poke:{button_slots + y:#x},{d.define('SLOT_BOW'):02x}",
                         f"poke:{button_slots + row + y:#x},00"]
    _copy_file(_harness_file(cfg, "mm3d", script + MM3D_SAVE, out), user)


# Termina Field from South Clock Town's owl statue: a scene change autosave writes the file at.
MM3D_FIELD = 0x5400
# Six full hearts, which a new file does not have: the loaded file shows them if it is the one saved.
MM3D_HEARTS = 0x60


def _mm3d_autosave_file(cfg: Config, user: Path, out: Path) -> None:
    """File 1 with six hearts, written by autosave on the scene change into Termina Field."""
    d = symbols.load(cfg.repo, "mm3d")
    save = symbols.global_(d, "gCommonData") + symbols.offset(d, "CommonData.save")
    hearts = MM3D_HEARTS.to_bytes(2, "little").hex()
    script = list(MM3D_BOOT) + [f"poke:{save + symbols.offset(d, 'SaveData.healthCapacity'):#x},{hearts}",
                                f"poke:{save + symbols.offset(d, 'SaveData.health'):#x},{hearts}",
                                "option:autosave,1", f"warp:{MM3D_FIELD:#x}", "wait:12"]
    _copy_file(_harness_file(cfg, "mm3d", script, out), user)


def _mm3d_reload(session: Session, pad: Gamepad) -> None:
    """File 1, as autosave wrote it, loaded in the window."""
    _mm3d_file_1(session)
    session.shot("loaded")


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


def _gaps(session: Session, pad: Gamepad) -> None:
    """OoT3D's inputs its text names: inside the Deku Tree the pad's View opens the map and closes
    it, as M does; in the item screen the right mouse button turns to Gear and the middle one
    back; R brings the ocarina out and R again its song list."""
    _oot3d_file_1(session)
    session.shot("play")
    for name, press in (("pad-view", lambda: pad.button("select")), ("key-map", lambda: session.key("play", "map"))):
        press()
        session.wait(2)
        session.shot(name)
        press()
        session.wait(2)
        session.shot(f"{name}-closed")
    session.key("play", "item_screen")
    session.wait(2)
    session.shot("items")
    session.mouse_button("shield")
    session.wait(2)
    session.shot("gear")
    session.mouse_button("target")
    session.wait(2)
    session.shot("items-again")
    session.key("play", "item_screen")
    session.wait(2)
    session.shot("play-again")
    session.key("play", "ocarina")
    session.wait(3)
    session.shot("ocarina")
    session.key("name", "zr")
    session.wait(2)
    session.shot("song-list")


def _notebook_mm3d(session: Session, pad: Gamepad) -> None:
    """MM3D with the Bombers' Notebook: the pad's View opens the map and View again the notebook,
    B goes back to play; the same with M."""
    _mm3d_file_1(session)
    for name, press in (("pad-view", lambda: pad.button("select")), ("key-map", lambda: session.key("play", "map"))):
        press()
        session.wait(3)
        session.shot(f"{name}-map")
        press()
        session.wait(3)
        session.shot(f"{name}-notebook")
        pad.button("b")
        session.wait(3)
        session.shot(f"{name}-play")


def _map_zoom_mm3d(session: Session, pad: Gamepad) -> None:
    """MM3D with every region's map: the minimap in play at the top screen's bottom right; on the
    Map screen the pad's X zooms in at the loupe and out
    again, the left stick moves the loupe; the same with the keyboard's X by name and its stick
    keys, the prompts shown as caps."""
    _mm3d_file_1(session)
    session.shot("play-minimap")
    pad.button("select")
    session.wait(3)
    session.shot("pad-map")
    pad.stick(0, -20000, 0.4)
    session.wait(1)
    session.shot("pad-loupe-up")
    pad.button("x")
    session.wait(3)
    session.shot("pad-zoom-in")
    pad.button("x")
    session.wait(3)
    session.shot("pad-zoom-out")
    pad.button("b")
    session.wait(3)
    session.key("play", "map")
    session.wait(3)
    session.shot("key-map")
    session.key("stick", "down", 0.4)
    session.wait(1)
    session.shot("key-loupe-down")
    session.key("name", "x")
    session.wait(3)
    session.shot("key-zoom-in")
    session.key("name", "x")
    session.wait(3)
    session.shot("key-zoom-out")


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


def _aim_turns(session: Session, moves: tuple[tuple[str, int, int], ...] = AIM_MOVES) -> None:
    """Mouse aim once the look is first person: the app takes the mouse, each move turns the
    view by 0.05 degrees a count at a sensitivity of 1 (app/src/mouse_aim.hpp), and the last one
    comes back to the start."""
    session.expect("mouse aim:")
    session.wait(1)
    session.shot("first-person")
    for name, dx, dy in moves:
        session.mouse(dx, dy)
        session.wait(1)
        session.shot(name)


def _aim_released(session: Session, moves: tuple[tuple[str, int, int], ...] = AIM_MOVES) -> None:
    """The app's own count of the mouse's travel while it aimed is what the test sent."""
    travel = (sum(abs(dx) for _, dx, _ in moves), sum(abs(dy) for _, _, dy in moves))
    line = session.expect(": aim took")
    if not line.endswith(f"aim took {travel[0]} by {travel[1]} counts"):
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


CAMERA_MOVES = (("right-400", 400, 0), ("left-400", -400, 0), ("up-200", 0, -200), ("down-200", 0, 200))


def _camera_turns(session: Session, moves: tuple[tuple[str, int, int], ...] = CAMERA_MOVES) -> None:
    """In play the app takes the mouse for the camera, and each move turns it (app/src/mouse_aim.hpp)."""
    session.shot("file")
    session.expect("mouse camera:")
    session.wait(1)
    session.shot("play")
    for name, dx, dy in moves:
        session.mouse(dx, dy)
        session.wait(1)
        session.shot(name)


def _camera(session: Session, pad: Gamepad) -> None:
    """OoT3D: the free camera inside the Deku Tree turned by real mouse motion."""
    _oot3d_file_1(session)
    _camera_turns(session)


def _camera_mm3d(session: Session, pad: Gamepad) -> None:
    """MM3D: the game's own C-stick camera at South Clock Town's owl statue turned by real mouse motion."""
    _mm3d_file_1(session)
    _camera_turns(session)


# The mouse sensitivity the fast scenarios choose, and the travel that turns the view as far as
# AIM_MOVES and CAMERA_MOVES at 1.
FAST = 2.0


def _store_options(cfg: Config, user: Path, game: str, choices: dict[str, int]) -> None:
    """The player's `choices` (option id: choice index) for `game`, kept where the options menu
    keeps them (app/src/main.cpp, storedOptions)."""
    stored = user / "options" / f"{cfg.rom(GameSet.named(cfg.repo, game).title_id).stem}.json"
    stored.parent.mkdir(parents=True, exist_ok=True)
    stored.write_text(json.dumps(choices) + "\n")


def _fast_option(cfg: Config, user: Path, game: str) -> None:
    """The player's choice of FAST for `game`."""
    scales = next(o.scales for o in options.load(cfg.repo, game) if o.id == "mouse_sensitivity")
    _store_options(cfg, user, game, {"mouse_sensitivity": scales.index(FAST)})


def _fast_moves(moves: tuple[tuple[str, int, int], ...]) -> tuple[tuple[str, int, int], ...]:
    return tuple((name, round(dx / FAST), round(dy / FAST)) for name, dx, dy in moves)


def _aim_fast(session: Session, pad: Gamepad) -> None:
    """OoT3D at twice the mouse sensitivity: half of `aim`'s travel turns the first-person view
    as far, frame for frame."""
    _oot3d_file_1(session)
    session.key("play", "first_person")
    _aim_turns(session, _fast_moves(AIM_MOVES))
    session.mouse_button("sword")
    _aim_released(session, _fast_moves(AIM_MOVES))


def _camera_fast(session: Session, pad: Gamepad) -> None:
    """OoT3D at twice the mouse sensitivity: half of `camera`'s travel turns the free camera as far."""
    _oot3d_file_1(session)
    _camera_turns(session, _fast_moves(CAMERA_MOVES))


def _fire(session: Session, pad: Gamepad) -> None:
    """OoT3D: the left mouse button swings the sword in play; slot 1 aims the Slingshot, and
    the left button then fires a seed (the count falls by one); E puts it away and the left
    button is the sword again."""
    _oot3d_file_1(session)
    session.shot("play")
    session.mouse_button("sword")
    session.wait(0.3)
    session.shot("sword")
    session.wait(1.5)
    session.key("slot", "0")
    session.expect("mouse aim:")
    session.wait(1.5)
    session.shot("aimed")
    session.mouse_button("sword")
    session.wait(1)
    session.shot("fired")
    session.mouse_button("sword")
    session.wait(1)
    session.shot("fired-again")
    session.key("play", "action")
    session.expect("mouse camera:")
    session.wait(1.5)
    session.mouse_button("sword")
    session.wait(0.3)
    session.shot("sword-again")


def _fire_mm3d(session: Session, pad: Gamepad) -> None:
    """MM3D: the left mouse button swings the sword in play; slot 1 aims the bow, the left button
    held draws it and let go fires an arrow (the count falls by one); E puts it away."""
    _mm3d_file_1(session)
    session.shot("play")
    session.mouse_button("sword")
    session.wait(0.3)
    session.shot("sword")
    session.wait(1.5)
    session.key("slot", "0")
    session.expect("mouse aim:")
    session.wait(1.5)
    session.shot("aimed")
    session.mouse_button("sword", 1.0)
    session.wait(1)
    session.shot("fired")
    session.mouse_button("sword", 1.0)
    session.wait(1)
    session.shot("fired-again")
    session.key("play", "action")
    session.expect("mouse camera:")
    session.wait(1.5)
    session.mouse_button("sword")
    session.wait(0.3)
    session.shot("sword-again")


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


def _stage(session: Session, pad: Gamepad) -> None:
    """The picker's stage (docs/plans/title-stage.md) by keys: OoT3D runs wide, Right moves the edge
    and runs MM3D with OoT3D held grey, Left brings OoT3D back where it stopped, Right and A pick
    MM3D, whose edge slides out while it plays on."""
    session.expect("picker: on Ocarina")
    session.wait(8)
    session.shot("oot3d-running")
    session.key("menu", "right")
    session.expect("picker: on Majora")
    session.wait(0.1)
    session.shot("edge-sliding")
    session.wait(6)
    session.shot("mm3d-running")
    session.key("menu", "left")
    session.expect("picker: on Ocarina")
    session.wait(2)
    session.shot("oot3d-again")
    session.key("menu", "right")
    session.expect("picker: on Majora")
    session.wait(2)
    session.key("menu", "a")
    session.expect("picker: picked Majora")
    session.wait(0.15)
    session.shot("pick-sliding")
    session.expect("booting")
    session.wait(3)
    session.shot("mm3d-playing")


# How long the picker's stage takes to load both games before it reads keys.
PICKER_LOADED = 8


def _options(session: Session, pad: Gamepad) -> None:
    """The picker's options: X on MM3D opens them, A turns the cutscene skip off, Up scrolls to the
    Fullscreen row and A and the fullscreen key flip it, and Start then leaves the opening narration
    running."""
    game = session.expect("picker: on ")
    # The stage takes keys once both games are loaded.
    session.wait(PICKER_LOADED)
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
    while not row.endswith("options: row fullscreen"):
        session.key("menu", "up")
        row = session.expect("options: row ")
    session.shot("fullscreen-row")
    session.key("menu", "a")
    session.expect("fullscreen off")
    session.shot("fullscreen-off")
    session.key("window", "fullscreen")
    session.expect("fullscreen on")
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
    """The picker's options for OoT3D: X opens them, Down and A turn Quick Text off, the Frame
    interpolation row is there and on, and File 1 then boots with the option written and the
    in-betweens on. Each step waits on the menu's own log line."""
    game = session.expect("picker: on ")
    session.wait(PICKER_LOADED)
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
    while not row.endswith("options: row frame_interpolation"):
        # A press right after a release would read as held.
        session.wait(0.2)
        session.key("menu", "down")
        row = session.expect("options: row ")
    session.shot("frame-interpolation-row")
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
    session.expect("frame interpolation on")


# Direct launches of one game, and the installed shortcut's picker.
# The interpolation scenarios turn the camera through this many of the app's pacing reports
# (app/src/player.hpp, kPacingReport), wholly in play.
INTERP_REPORTS = 2
INTERP_REPORT_SECONDS = 10
INTERP_TURN = (40, 0)
INTERP_TURN_EVERY = 0.25


def _interp(session: Session, expected: str, file_1: Callable[[Session], None]) -> None:
    """A game's File 1 with the camera turning; the app's pacing reports made wholly in play
    (flips, the in-betweens among them and ticks a second) are kept in pacing.txt."""
    session.expect(f"frame interpolation {expected}", 60)
    file_1(session)
    session.expect("mouse camera:")
    session.shot("play")
    # The report under way when play starts covers the boot too.
    session.expect("pacing ", INTERP_REPORT_SECONDS + 5)
    turns = round((INTERP_REPORTS * INTERP_REPORT_SECONDS + 1) / INTERP_TURN_EVERY)
    for _ in range(turns):
        session.mouse(*INTERP_TURN)
        session.wait(INTERP_TURN_EVERY)
    session.shot("turned")
    reports = [session.expect("pacing ", INTERP_REPORT_SECONDS) for _ in range(INTERP_REPORTS)]
    (session.out / "pacing.txt").write_text("\n".join(reports) + "\n")


# How long the texture scenarios record play after Link's walk.
INTERP_TEXTURES_SECONDS = 10


def _interp_textures(session: Session, file_1: Callable[[Session], None], walk: float) -> None:
    """A game's File 1 loaded from the title screen with every present recorded (--record), then
    Link walked toward the camera for `walk` seconds: the scene's custom textures decode while it
    plays, and each in-between sits beside the real frames it was made between (presents/)."""
    session.expect("frame interpolation on", 60)
    file_1(session)
    session.key("stick", "down", walk)
    session.wait(INTERP_TEXTURES_SECONDS)


# How far and how often the turning scenarios swing the camera, and for how long they walk.
FAST_TURN = (400, 0)
FAST_TURN_EVERY = 0.2
FAST_TURN_SECONDS = 12


def _interp_turn(session: Session, file_1: Callable[[Session], None]) -> None:
    """A game's File 1 with every present recorded (--record), then the camera swung fast with the
    mouse while Link runs: views the scene's custom textures were not yet drawn in come into the
    in-betweens as the real frames bring them (presents/)."""
    session.expect("frame interpolation on", 60)
    file_1(session)
    for turn in range(round(FAST_TURN_SECONDS / FAST_TURN_EVERY)):
        if turn % 10 == 0:
            session.key("stick", "up", FAST_TURN_EVERY * 5)
        session.mouse(*FAST_TURN)
        session.wait(FAST_TURN_EVERY)


# Routes through several scenes (tools/z3dmods/routes/): the circle pad a harness walk held from the
# scenario's File 1, [x, y, seconds] each ([0, 0, s] a wait), and ["mouse", dx, seconds] turns.
ROUTES = Path(__file__).parent / "routes"
INTERP_SCENES_SETTLE_SECONDS = 10


def _interp_scenes(session: Session, pad: Gamepad, file_1: Callable[[Session], None], route: str) -> None:
    """A game's File 1 with every present recorded (--record), then a route through several scenes,
    the camera swung fast and back after each load: the in-betweens beside each load show whether
    the next scene's custom textures came in later than its real frames' (presents/). The holds
    play open-loop on wall-clock time, so the window can leave the route after a few loads."""
    session.expect("frame interpolation on", 60)
    # Attached before the route: attaching takes seconds the recorded holds do not wait for.
    pad.steer(0, 0)
    file_1(session)
    # The routes start from Link standing after the scene's title card, which a window reaches later.
    session.wait(INTERP_SCENES_SETTLE_SECONDS)
    for x, y, seconds in json.loads((ROUTES / route).read_text())["route"]:
        if x == "mouse":
            session.mouse(y, 0)
        else:
            pad.steer(x, y)
        session.wait(seconds)
    pad.steer(0, 0)
    session.wait(INTERP_TEXTURES_SECONDS)


DIRECT = {"oot3d": _oot3d, "mm3d": _mm3d, "verbs-mm3d": _verbs_mm3d, "gaps": _gaps, "notebook-mm3d": _notebook_mm3d, "map-zoom-mm3d": _map_zoom_mm3d, "masks-mm3d": _masks_mm3d, "autosave-mm3d": _mm3d_reload, "camera": _camera, "camera-mm3d": _camera_mm3d, "hotswap": _hotswap, "slots": _slots, "slots-small": _slots,
          "slots-mm3d": _slots_mm3d, "slots-mm3d-small": _slots_mm3d, "aim": _aim, "aim-mm3d": _aim_mm3d,
          "aim-fast": _aim_fast, "camera-fast": _camera_fast, "camera-mq": _camera, "fire": _fire,
          "fire-mm3d": _fire_mm3d, "interp": lambda session, pad: _interp(session, "on", _oot3d_file_1),
          "interp-off": lambda session, pad: _interp(session, "off", _oot3d_file_1),
          "interp-mm3d": lambda session, pad: _interp(session, "on", _mm3d_file_1),
          "interp-textures": lambda session, pad: _interp_textures(session, _oot3d_file_1, 4),
          "interp-textures-mm3d": lambda session, pad: _interp_textures(session, _mm3d_file_1, 6),
          "interp-turn": lambda session, pad: _interp_turn(session, _oot3d_file_1),
          "interp-turn-mm3d": lambda session, pad: _interp_turn(session, _mm3d_file_1),
          "interp-scenes": lambda session, pad: _interp_scenes(session, pad, _oot3d_file_1, "oot3d-kokiri.json"),
          "interp-scenes-mm3d": lambda session, pad: _interp_scenes(session, pad, _mm3d_file_1,
                                                                    "mm3d-clock-town.json")}
# The game a direct scenario launches, when it is not the scenario's name.
DIRECT_GAME = {"hotswap": "mm3d", "slots": "oot3d", "slots-small": "oot3d", "slots-mm3d": "mm3d", "slots-mm3d-small": "mm3d",
               "aim": "oot3d", "aim-mm3d": "mm3d", "verbs-mm3d": "mm3d", "gaps": "oot3d", "notebook-mm3d": "mm3d", "map-zoom-mm3d": "mm3d", "masks-mm3d": "mm3d",
               "autosave-mm3d": "mm3d", "camera": "oot3d", "camera-mm3d": "mm3d", "aim-fast": "oot3d",
               "camera-fast": "oot3d", "camera-mq": "oot3d", "fire": "oot3d", "fire-mm3d": "mm3d",
               "interp": "oot3d", "interp-off": "oot3d", "interp-mm3d": "mm3d", "interp-textures": "oot3d",
               "interp-textures-mm3d": "mm3d", "interp-turn": "oot3d", "interp-turn-mm3d": "mm3d",
               "interp-scenes": "oot3d", "interp-scenes-mm3d": "mm3d"}
# The direct scenarios that record every present into presents/ (app --record).
RECORDED = ("interp-textures", "interp-textures-mm3d", "interp-turn", "interp-turn-mm3d", "interp-scenes",
            "interp-scenes-mm3d")
# The window of a scenario that is not WIDTH by HEIGHT.
SIZES = {"slots-small": SMALL, "slots-mm3d-small": SMALL}
# What a scenario puts in its user folder before the app starts.
PREPARE = {"slots": _oot3d_slots_file, "slots-small": _oot3d_slots_file, "slots-mm3d": _mm3d_slots_file,
           "slots-mm3d-small": _mm3d_slots_file, "aim": _oot3d_slots_file, "aim-mm3d": _mm3d_slots_file, "verbs-mm3d": _mm3d_slots_file, "masks-mm3d": _mm3d_slots_file, "gaps": _oot3d_slots_file,
           "notebook-mm3d": lambda cfg, user, out: _mm3d_slots_file(cfg, user, out, notebook=True),
           "map-zoom-mm3d": lambda cfg, user, out: _mm3d_slots_file(cfg, user, out, maps=True),
           "autosave-mm3d": _mm3d_autosave_file, "camera": _oot3d_slots_file, "camera-mm3d": _mm3d_slots_file,
           "aim-fast": lambda cfg, user, out: (_oot3d_slots_file(cfg, user, out), _fast_option(cfg, user, "oot3d")),
           "camera-fast": lambda cfg, user, out: (_oot3d_slots_file(cfg, user, out), _fast_option(cfg, user, "oot3d")),
           "camera-mq": lambda cfg, user, out: _oot3d_slots_file(cfg, user, out, master_quest=True),
           "fire": lambda cfg, user, out: _oot3d_slots_file(cfg, user, out, aimed_on_y=True),
           "fire-mm3d": lambda cfg, user, out: _mm3d_slots_file(cfg, user, out, aimed_on_y=True),
           "interp-off": lambda cfg, user, out: _store_options(cfg, user, "oot3d", {"frame_interpolation": 0}),
           "interp-mm3d": _mm3d_slots_file, "interp-textures-mm3d": _mm3d_slots_file, "interp-turn-mm3d": _mm3d_slots_file,
           "interp-scenes-mm3d": _mm3d_slots_file,
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
SCENARIOS = {**DIRECT, **PICKER, **PACKAGE, "picker": _picker, "stage": _stage, "options": _options, "options-oot3d": _options_oot3d}


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
        args = play.launch_args(cfg, DIRECT_GAME.get(scenario, scenario), user)
        if scenario in RECORDED:
            # At the 3DS's own scale, so that each present is saved as fast as the window shows it.
            name = DIRECT_GAME[scenario]
            (out / "presents").mkdir(parents=True)
            args = app.launch_args(cfg, GameSet.named(cfg.repo, name), user, 1) + [
                "--title", play.TITLES[name], "--app-id", play.DESKTOP_ID, "--record", str(out / "presents")]
        return Session(out, args, keys,
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
    with _session(cfg, scenario, out) as session:
        SCENARIOS[scenario](session, Gamepad(session))
    return out
