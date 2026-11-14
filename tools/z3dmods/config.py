"""The one reader of the environment and of the local settings: a checkout's gitignored
env-assets/local.json, or a package's folders file in the player's data (package.py)."""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]


@dataclass(frozen=True)
class Config:
    repo: Path
    # The player's own third-party mod downloads, searched by the sets' hashes (source_check.py).
    mod_sources: Path
    azahar_data: Path
    azahar_config: Path
    # Standalone Azahar, which the checkout's tools run; a package has none, nor a machine without openSU's.
    azahar_appimage: Path | None
    roms: dict[str, Path]
    # The SomeoneIsWorking/azahar checkout the app builds inside (local.json "azahar_source").
    azahar_source: Path | None
    # Third-party builds shared across repos, one prefix per revision and toolchain (dev/ in the home folder).
    dependencies: Path
    # The z3d app (app/) and the data it plays from (saves, system files, a link to load/).
    app_binary: Path
    app_data: Path
    # The desktop session's display variables, so a tool started outside it reaches it too.
    session_env: dict[str, str]
    # The shared port-assets checkout ($PORT_ASSETS_DIR, else shared/port-assets beside this repo's parent).
    port_assets: Path
    # This program as another program starts it: the app (its --tools), the desktop entry.
    tool_command: tuple[str, ...]
    # The picker's font and the folder of the app's shared libraries, where a package brings them.
    bundled_font: Path | None = None
    libraries: Path | None = None
    # A package (package.py), whose sets its picker keeps up with it; a checkout's are the
    # maintainer's to install.
    packaged: bool = False

    def rom(self, title_id: str) -> Path:
        if title_id not in self.roms:
            raise KeyError(f"no dump of {title_id} is known")
        return self.roms[title_id]

    def app_env(self) -> dict[str, str]:
        """The environment the app runs in: the player's session and the package's libraries."""
        env = dict(os.environ, **self.session_env)
        if self.libraries is not None:
            env["LD_LIBRARY_PATH"] = str(self.libraries)
        return env

    def mods_dir(self, title_id: str) -> Path:
        return self.azahar_data / "load" / "mods" / title_id

    def textures_dir(self, title_id: str) -> Path:
        return self.azahar_data / "load" / "textures" / title_id

    def ui_font(self) -> Path:
        """The title picker's outline font: the package's, else fontconfig's."""
        if self.bundled_font is not None:
            return self.bundled_font
        if shutil.which("fc-match") is None:
            raise RuntimeError("fc-match is missing: sudo dnf install fontconfig (Fedora), "
                               "sudo apt install fontconfig (Debian/Ubuntu)")
        found = subprocess.run(["fc-match", "-f", "%{file}", UI_FONT], capture_output=True, text=True,
                               check=True).stdout.strip()
        if not found.endswith((".ttf", ".otf")):
            raise RuntimeError(f"fontconfig has no outline font for {UI_FONT!r}: sudo dnf install "
                               "google-noto-sans-fonts (Fedora), sudo apt install fonts-noto-core (Debian/Ubuntu)")
        return Path(found)

    def key_font(self) -> Path:
        """port-assets' key-label typeface, which new HUD lettering uses."""
        font = self.port_assets / "fonts" / "NotoSans-Bold-keys.ttf"
        if not font.exists():
            raise RuntimeError(f"no port-assets checkout at {self.port_assets}: clone "
                               "github.com/SomeoneIsWorking/port-assets there or set PORT_ASSETS_DIR")
        return font

    def glyph_set(self) -> Path:
        """port-assets' Xbox pad glyphs (SVG), which the HUD's button prompts are drawn from."""
        glyphs = self.port_assets / "sets" / "gamepad-xbox360"
        if not glyphs.is_dir():
            raise RuntimeError(f"no port-assets checkout at {self.port_assets}: clone "
                               "github.com/SomeoneIsWorking/port-assets there or set PORT_ASSETS_DIR")
        return glyphs


UI_FONT = "Noto Sans:bold"
# Moves the app's data (saves included) for a test run of the installed shortcut (input_test.py).
APP_DATA_ENV = "Z3D_APP_DATA"
# Names another folder of mod downloads for a test run.
MOD_SOURCES_ENV = "Z3D_MOD_SOURCES"
SESSION_KEYS = ("WAYLAND_DISPLAY", "DISPLAY", "XAUTHORITY", "XDG_SESSION_TYPE")


def _session_env() -> dict[str, str]:
    if os.environ.get("WAYLAND_DISPLAY") or os.environ.get("DISPLAY"):
        return {key: os.environ[key] for key in SESSION_KEYS if key in os.environ}
    # A machine without a user session (a build runner) has no display to name.
    shown = subprocess.run(["systemctl", "--user", "show-environment"], capture_output=True,
                           text=True, check=False).stdout
    found = dict(line.split("=", 1) for line in shown.splitlines() if "=" in line)
    return {key: found[key] for key in SESSION_KEYS if key in found}


# A package's folder of what a checkout builds from source: each game's patch and names, the glyphs.
PREBUILT = "prebuilt"
# What marks REPO as a package (package.py) and not a checkout.
PACKAGE_MARK = "package.json"
# The player's folders, in a package's app data: {"roms": <folder>, "mods": <folder>}.
FOLDERS = "folders.json"


def _packaged(data_home: Path) -> Config:
    """A package's layout (package.py): everything beside the tools, the player's data in
    `user/` inside it when that exists (a portable install), else in their data folder."""
    portable = REPO / "user"
    app_data = Path(os.environ.get(APP_DATA_ENV) or (portable if portable.is_dir() else data_home / "z3d"))
    folders = json.loads((app_data / FOLDERS).read_text()) if (app_data / FOLDERS).exists() else {}
    return Config(
        repo=REPO,
        mod_sources=Path(os.environ.get(MOD_SOURCES_ENV) or folders.get("mods") or app_data / "mods"),
        azahar_data=app_data / "Azahar",
        azahar_config=app_data / "Azahar" / "config",
        azahar_appimage=None,
        roms={},
        azahar_source=None,
        dependencies=REPO,
        app_binary=REPO / "bin" / "z3d",
        app_data=app_data,
        session_env=_session_env(),
        port_assets=REPO / "assets",
        tool_command=(sys.executable, "-I", "-m", "z3dmods.packed"),
        bundled_font=next((REPO / "fonts").glob("*.ttf")),
        libraries=REPO / "lib",
        packaged=True,
    )


def load() -> Config:
    home = Path.home()
    data_home = Path(os.environ.get("XDG_DATA_HOME") or home / ".local/share")
    if (REPO / PACKAGE_MARK).exists():
        return _packaged(data_home)
    config_home = Path(os.environ.get("XDG_CONFIG_HOME") or home / ".config")
    uv = shutil.which("uv")
    if uv is None:
        raise RuntimeError("uv is not on PATH")
    env_assets = REPO / "env-assets"
    local = json.loads((env_assets / "local.json").read_text())
    installed = data_home / "opensu/emulators/azahar/installed.json"
    return Config(
        repo=REPO,
        mod_sources=Path(os.environ.get(MOD_SOURCES_ENV) or env_assets / "downloads"),
        azahar_data=data_home / "azahar-emu",
        azahar_config=config_home / "azahar-emu",
        azahar_appimage=installed.parent / json.loads(installed.read_text())["file"] if installed.exists() else None,
        roms={tid: Path(path) for tid, path in local["roms"].items()},
        azahar_source=Path(local["azahar_source"]) if "azahar_source" in local else None,
        dependencies=home / "dev",
        app_binary=REPO / "build" / "app" / "z3d",
        app_data=Path(os.environ.get(APP_DATA_ENV) or data_home / "z3d-mods"),
        session_env=_session_env(),
        port_assets=Path(os.environ.get("PORT_ASSETS_DIR") or REPO.parent.parent / "shared" / "port-assets"),
        tool_command=(uv, "--directory", str(REPO), "run", "--frozen", "z3dmods"),
    )
