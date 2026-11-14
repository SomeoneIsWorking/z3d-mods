"""The container the package's app and core are built in (packaging/Containerfile), so the
package runs on desktops older than this machine: the image's glibc and libstdc++ are the
oldest it needs. The checkout and the shared checkouts the app builds from are mounted at their
own paths, and commands run in it as this user.
"""

from __future__ import annotations

import hashlib
import shutil
import subprocess
from pathlib import Path

from z3dmods import app
from z3dmods.config import Config

IMAGE = "localhost/z3d-package-build"
CONTAINERFILE = "packaging/Containerfile"
# The image's distribution, in the name of the core's prefix.
TOOLCHAIN = "clang-ubuntu24.04"
# The newest glibc the image links against: what a desktop must have (package.py checks it).
GLIBC_FLOOR = (2, 39)
# The shared checkouts app/CMakeLists.txt builds from, under the home folder.
CHECKOUTS = ("repo/lucent", "repo/shared/setup-ui", "repo/RmlUi")


def image(cfg: Config) -> str:
    """The build image, built from the Containerfile unless one of its content exists."""
    if shutil.which("podman") is None:
        raise RuntimeError("podman is missing: sudo dnf install podman (Fedora), sudo apt install podman (Debian/Ubuntu)")
    recipe = cfg.repo / CONTAINERFILE
    tag = f"{IMAGE}:{hashlib.sha256(recipe.read_bytes()).hexdigest()[:12]}"
    if subprocess.run(["podman", "image", "exists", tag], check=False).returncode != 0:
        subprocess.run(["podman", "build", "-t", tag, "-f", str(recipe), str(recipe.parent)], check=True)
    return tag


def _command(cfg: Config, command: list[str]) -> list[str]:
    home = Path.home()
    cores = cfg.dependencies / "azahar-core"
    cores.mkdir(parents=True, exist_ok=True)
    mounts = [f"{cfg.repo}:{cfg.repo}", f"{cores}:{cores}", f"{app.azahar_source(cfg)}:{app.azahar_source(cfg)}:ro",
              *(f"{home / checkout}:{home / checkout}:ro" for checkout in CHECKOUTS)]
    words = ["podman", "run", "--rm", "--userns=keep-id", "--security-opt", "label=disable", "-e", f"HOME={home}"]
    for mount in mounts:
        words += ["-v", mount]
    return [*words, image(cfg), *command]


def run(cfg: Config, command: list[str]) -> None:
    subprocess.run(_command(cfg, command), check=True)


def output(cfg: Config, command: list[str]) -> str:
    return subprocess.run(_command(cfg, command), check=True, capture_output=True, text=True).stdout


BUILDER = app.Builder(TOOLCHAIN, lambda cfg: cfg.repo / "build" / "package-app", run)
