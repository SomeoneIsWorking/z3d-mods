"""The app's key and mouse bindings, read from the app itself.

`z3d --keys` prints one "<table>\\t<name>\\t<key>" line per binding (app/src/pad_input.hpp
`listKeys`, app/src/game_window.hpp); a key is SDL's name for it, and a slot behind the chord is
"<chord key>+<key>". Here each becomes the X keysyms to hold, in order.
"""

from __future__ import annotations

import subprocess

from z3dmods import app
from z3dmods.config import Config

# SDL's names that are not the X keysym: every other key is its name, a letter in lower case.
KEYSYMS = {"Space": "space", "Backspace": "BackSpace", "Left Shift": "Shift_L", "Right Shift": "Shift_R"}
# The table of mouse buttons: its "keys" are SDL's button numbers, which are X's too.
MOUSE = "mouse"


def _keysym(key: str) -> str:
    return KEYSYMS.get(key, key.lower() if len(key) == 1 else key)


class AppKeys:
    def __init__(self, cfg: Config) -> None:
        listing = subprocess.run([str(app.binary(cfg)), "--keys"], env=cfg.app_env(), check=True,
                                 capture_output=True, text=True).stdout
        self._bindings: dict[tuple[str, str], list[str]] = {}
        for line in listing.splitlines():
            table, name, key = line.split("\t")
            self._bindings.setdefault((table, name), []).append(key)

    def _first(self, table: str, name: str) -> str:
        if (table, name) not in self._bindings:
            raise KeyError(f"the app binds no key to {table} {name}")
        return self._bindings[table, name][0]

    def key(self, table: str, name: str) -> str:
        """SDL's name of the first key bound to `name` of `table`; a mouse button's number."""
        return self._first(table, name)

    @staticmethod
    def is_mouse(table: str) -> bool:
        return table == MOUSE

    def keysyms(self, table: str, name: str) -> list[str]:
        """The keys to hold for `name` of `table`, by its first binding."""
        return [_keysym(key) for key in self._first(table, name).split("+")]

    def mouse_button(self, name: str) -> int:
        """The mouse button of the play verb `name`."""
        return int(self._first(MOUSE, name))
