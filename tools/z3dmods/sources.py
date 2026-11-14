"""The checkouts the app is built from besides this one: where each lives, the revision
`z3dmods sources` fetches on a machine without them (the release workflow), and the commit a
build was made from, which a package names as its source (SOURCE.txt).
"""

from __future__ import annotations

import json
import shutil
import subprocess
import urllib.request
from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path

from z3dmods import app, config
from z3dmods.config import Config

# The shared checkouts app/CMakeLists.txt builds from, under the home folder.
HOME_CHECKOUTS = {"lucent": "repo/lucent", "setup-ui": "repo/shared/setup-ui", "RmlUi": "repo/RmlUi"}
# The AppImage runtime (github.com/AppImage/type2-runtime, MIT): the program a squashfs image of
# the package folder is appended to.
RUNTIME_RELEASE = "20251108"
RUNTIME_SHA256 = "2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d"
RUNTIME_URL = f"https://github.com/AppImage/type2-runtime/releases/download/{RUNTIME_RELEASE}/runtime-x86_64"


@dataclass(frozen=True)
class Source:
    name: str
    url: str
    # What a fetch checks out. A build names the commit its checkout is at, not this.
    revision: str
    folder: Callable[[Config], Path]
    submodules: bool = False


def _home(name: str) -> Callable[[Config], Path]:
    return lambda _cfg: Path.home() / HOME_CHECKOUTS[name]


def _azahar(cfg: Config) -> Path:
    if cfg.azahar_source is None:
        raise RuntimeError('set "azahar_source" in env-assets/local.json to the SomeoneIsWorking/azahar checkout')
    return cfg.azahar_source


SOURCES = (
    Source("azahar", "https://github.com/SomeoneIsWorking/azahar", app.AZAHAR_REVISION, _azahar, submodules=True),
    Source("lucent", "https://github.com/SomeoneIsWorking/lucent", "2597c3488b670d8f20eb463fc5a230e260cb7105",
           _home("lucent")),
    Source("setup-ui", "https://github.com/SomeoneIsWorking/setup-ui", "6798b0bf014a0e3c73a010566939bf44cb8259ed",
           _home("setup-ui")),
    Source("RmlUi", "https://github.com/mikke89/RmlUi", "ba95ffe8bfb6370efb2cdcca927eaad4710c5413", _home("RmlUi")),
    Source("port-assets", "https://github.com/SomeoneIsWorking/port-assets",
           "81f39c900cba9e9faac8707e5900dc71caa0f553", lambda cfg: cfg.port_assets),
)


def _git(folder: Path, *words: str) -> str:
    return subprocess.run(["git", "-C", str(folder), *words], check=True, capture_output=True, text=True).stdout.strip()


def built_from(folder: Path) -> str:
    """The commit the checkout `folder` is at, and whether its tracked files differ from it."""
    changed = _git(folder, "status", "--porcelain", "--untracked-files=no")
    return _git(folder, "rev-parse", "HEAD") + (" with uncommitted changes" if changed else "")


def runtime(cfg: Config) -> Path:
    return cfg.dependencies / "appimage-runtime" / RUNTIME_RELEASE / "runtime-x86_64"


def _local() -> None:
    """A checkout's local settings for a machine that has none: no dumps, the core beside it."""
    local = config.REPO / "env-assets" / "local.json"
    if not local.exists():
        local.parent.mkdir(exist_ok=True)
        local.write_text(json.dumps({"roms": {}, "azahar_source": str(config.REPO.parent / "azahar")}, indent=2) + "\n")


def fetch() -> list[Path]:
    """Clones every checkout this machine lacks at its revision, and downloads the AppImage
    runtime; what exists is left as it is. Returns what it made."""
    _local()
    cfg = config.load()
    made = []
    for source in SOURCES:
        folder = source.folder(cfg)
        if folder.exists():
            continue
        folder.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(["git", "clone", "--quiet", "--no-checkout", source.url, str(folder)], check=True)
        subprocess.run(["git", "-C", str(folder), "checkout", "--quiet", "--detach", source.revision], check=True)
        hook = folder / "hooks" / "pre-commit"
        if hook.exists():
            # The core's configure copies its hook in, and the build container cannot write the checkout.
            shutil.copyfile(hook, folder / ".git" / "hooks" / "pre-commit")
        if source.submodules:
            subprocess.run(["git", "-C", str(folder), "submodule", "update", "--quiet", "--init", "--recursive",
                            "--depth", "1"], check=True)
        made.append(folder)
    program = runtime(cfg)
    if not program.exists():
        program.parent.mkdir(parents=True, exist_ok=True)
        with urllib.request.urlopen(RUNTIME_URL) as reply:
            program.write_bytes(reply.read())
        made.append(program)
    return made
