"""A container that stands for a desktop of the oldest distribution the package runs on
(packaging/Containerfile.desktop): the input test starts the AppImage in it, on the display
of the test's Gamescope, with no network.
"""

from __future__ import annotations

import hashlib
import subprocess
from pathlib import Path

from z3dmods.config import Config

IMAGE = "localhost/z3d-older-desktop"
CONTAINERFILE = "packaging/Containerfile.desktop"
# The program a test session finds its display by: the container's own client.
PROGRAM = "podman"


def image(cfg: Config) -> str:
    recipe = cfg.repo / CONTAINERFILE
    tag = f"{IMAGE}:{hashlib.sha256(recipe.read_bytes()).hexdigest()[:12]}"
    if subprocess.run(["podman", "image", "exists", tag], check=False).returncode != 0:
        subprocess.run(["podman", "build", "-t", tag, "-f", str(recipe), str(recipe.parent)], check=True)
    return tag


def command(cfg: Config, writable: Path, readable: list[Path], env: dict[str, str], program: list[str]) -> list[str]:
    """`program` started in the container as this user, on the display the caller's environment
    names when it runs. `writable` and each of `readable` are mounted at their own paths. An
    AppImage unpacks itself there: a container has no FUSE."""
    words = ["podman", "run", "--rm", "--userns=keep-id", "--security-opt", "label=disable", "--network", "none",
             "--device", "/dev/dri", "-e", "DISPLAY", "-v", "/tmp/.X11-unix:/tmp/.X11-unix:ro",
             "-e", "APPIMAGE_EXTRACT_AND_RUN=1", "-e", f"HOME={writable}", "-v", f"{writable}:{writable}"]
    for path in readable:
        words += ["-v", f"{path}:{path}:ro"]
    for name, value in env.items():
        words += ["-e", f"{name}={value}"]
    return [*words, image(cfg), *program]
