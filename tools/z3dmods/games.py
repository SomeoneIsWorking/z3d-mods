"""Guest RAM layout the maintainer tools read, per title, resolved from the game's decomp."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

from z3dmods import symbols


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


def _oot3d(d: symbols.Decomp) -> Layout:
    cutscene = symbols.offset(d, "PlayState.csCtx")
    return Layout(
        play_pointer=symbols.global_(d, "gPlayState"),
        save_context=symbols.global_(d, "gSaveContext"),
        save_size=symbols.size(d, "SaveContext"),
        next_entrance=symbols.offset(d, "PlayState.nextEntranceIndex"),
        transition_trigger=symbols.offset(d, "PlayState.sceneLoadFlag"),
        cutscene_state=cutscene + symbols.offset(d, "CutsceneContext.state"),
        cutscene_frames=cutscene + symbols.offset(d, "CutsceneContext.frames"),
        cutscene_script=cutscene + symbols.offset(d, "CutsceneContext.segment"),
        scene=symbols.offset(d, "PlayState.sceneNum"),
    )


def _mm3d(d: symbols.Decomp) -> Layout:
    cutscene = symbols.offset(d, "PlayState.csCtx")
    return Layout(
        play_pointer=symbols.global_(d, "gPlayState"),
        save_context=symbols.global_(d, "gCommonData"),
        # start, scene and SaveData
        save_size=symbols.offset(d, "CommonData.save") + symbols.size(d, "SaveData"),
        next_entrance=symbols.offset(d, "PlayState.nextEntrance"),
        transition_trigger=symbols.offset(d, "PlayState.transitionTrigger"),
        cutscene_state=cutscene + symbols.offset(d, "CutsceneContext.state"),
        cutscene_frames=cutscene + symbols.offset(d, "CutsceneContext.frames"),
        cutscene_script=cutscene + symbols.offset(d, "CutsceneContext.script"),
        scene=symbols.offset(d, "PlayState.sceneNum"),
    )


_LAYOUTS = {"oot3d": _oot3d, "mm3d": _mm3d}


def layout(repo: Path, game: str) -> Layout:
    return _LAYOUTS[game](symbols.load(repo, game))


TRANSITION_START = 20
