"""Guest RAM layout the maintainer tools read, per title (the patch's own copy is patch/<game>/game.hpp)."""

from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class Layout:
    play_pointer: int
    save_context: int
    save_size: int
    next_entrance: int
    transition_trigger: int
    cutscene_state: int
    cutscene_frames: int
    scene: int


LAYOUTS = {
    # oot3d-decomp docs/ram_map.md; csCtx = play + 0x2298 (Randomizer z3D.h).
    "oot3d": Layout(
        play_pointer=0x0050AF34,
        save_context=0x00587958,
        save_size=0x15C4,
        next_entrance=0x5C32,
        transition_trigger=0x5C2D,
        cutscene_state=0x2298 + 0x08,
        cutscene_frames=0x2298 + 0x20,
        scene=0x104,
    ),
}

TRANSITION_START = 20
