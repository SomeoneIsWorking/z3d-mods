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
    cutscene_script: int
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
        cutscene_script=0x2298 + 0x04,
        scene=0x104,
    ),
    # Project Restoration's v1.0 code layout; csCtx = play + 0x2428 (patch/mm3d/game.hpp).
    "mm3d": Layout(
        play_pointer=0x00667890,
        save_context=0x007751D8,
        save_size=0x8 + 0x1A88,  # start, scene, SaveData
        next_entrance=0xC52E,
        transition_trigger=0xC529,
        cutscene_state=0x2428 + 0x264,
        cutscene_frames=0x2428 + 0x20,
        cutscene_script=0x2688,
        scene=0x148,
    ),
}

TRANSITION_START = 20
