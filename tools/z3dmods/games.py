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
    # OoT3D only: PlayState offsets of the player actor pointer and the main camera's look-at and
    # eye, and the actor's world position.
    player: int | None = None
    actor_position: int | None = None
    # A teleport writes all three: the player restores its position from home while it stands,
    # and the background check runs from the previous position to the new one.
    actor_previous: int | None = None
    actor_home: int | None = None
    camera_at: int | None = None
    camera_eye: int | None = None
    # MM3D only: where the game keeps the touch screen that is up, the table of play's screens,
    # and the main and blank ones' indices in it.
    active_screen: int | None = None
    screens: int | None = None
    screen_count: int | None = None
    main_screen: int | None = None
    black_screen: int | None = None


# ACTORCAT_PLAYER, the player's actor list.
PLAYER_CATEGORY = 2


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
        player=(symbols.offset(d, "PlayState.actorCtx") + symbols.offset(d, "ActorContext.actorLists")
                + PLAYER_CATEGORY * symbols.size(d, "ActorListEntry") + symbols.offset(d, "ActorListEntry.head")),
        actor_position=symbols.offset(d, "Actor.world"),
        actor_previous=symbols.offset(d, "Actor.prevPos"),
        actor_home=symbols.offset(d, "Actor.home"),
        camera_at=symbols.offset(d, "PlayState.mainCamera") + symbols.offset(d, "Camera.at"),
        camera_eye=symbols.offset(d, "PlayState.mainCamera") + symbols.offset(d, "Camera.eye"),
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
        active_screen=symbols.global_(d, "gUiScreenContext") + symbols.offset(d, "UiScreenContext.activeScreen"),
        screens=symbols.global_(d, "gUiScreens"),
        screen_count=d.define("UI_SCREEN_COUNT"),
        main_screen=d.define("UI_SCREEN_MAIN"),
        black_screen=d.define("UI_SCREEN_BLACK"),
    )


_LAYOUTS = {"oot3d": _oot3d, "mm3d": _mm3d}


def layout(repo: Path, game: str) -> Layout:
    return _LAYOUTS[game](symbols.load(repo, game))


TRANSITION_START = 20
