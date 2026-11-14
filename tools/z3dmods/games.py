"""Guest RAM layout the maintainer tools read, per title, resolved from the game's decomp."""

from __future__ import annotations

import tomllib
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
    # PlayState offsets of the player actor pointer and the camera's look-at and eye, and the
    # actor's world position.
    player: int | None = None
    actor_position: int | None = None
    # A teleport writes all three: the player restores its position from home while it stands,
    # and the background check runs from the previous position to the new one.
    actor_previous: int | None = None
    actor_home: int | None = None
    camera_at: int | None = None
    camera_eye: int | None = None
    # MM3D only: where the game keeps the touch screen that is up, the table of play's screens,
    # and where the app's overlay layout puts the touch screen for each entry (screen_overlay.hpp).
    active_screen: int | None = None
    screens: int | None = None
    # overlay.toml's layouts, the entry of each of the table's screens and the one outside play.
    screen_layouts: dict[str, dict] | None = None
    screen_placements: tuple[dict[str, str], ...] | None = None
    screen_outside: str | None = None
    # hud.toml as the app reads it (app/src/hud.hpp): elements with their guest addresses.
    hud: dict[str, object] | None = None
    # CommonData.hudVisibility and its values that leave the main touch screen blank or dimmed.
    hud_visibility: int | None = None
    hud_hidden: tuple[int, ...] | None = None


# ACTORCAT_PLAYER, the player's actor list.
PLAYER_CATEGORY = 2


def _player_list_head(d: symbols.Decomp) -> int:
    """PlayState offset of the player category's first actor."""
    return (symbols.offset(d, "PlayState.actorCtx") + symbols.offset(d, "ActorContext.actorLists")
            + PLAYER_CATEGORY * symbols.size(d, "ActorListEntry") + symbols.offset(d, "ActorListEntry.head"))


def _oot3d(_repo: Path, d: symbols.Decomp) -> Layout:
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
        player=_player_list_head(d),
        actor_position=symbols.offset(d, "Actor.world"),
        actor_previous=symbols.offset(d, "Actor.prevPos"),
        actor_home=symbols.offset(d, "Actor.home"),
        camera_at=symbols.offset(d, "PlayState.mainCamera") + symbols.offset(d, "Camera.at"),
        camera_eye=symbols.offset(d, "PlayState.mainCamera") + symbols.offset(d, "Camera.eye"),
    )


def _overlay(repo: Path, game: str, d: symbols.Decomp) -> dict[str, object]:
    """overlay.toml as the app reads it: each of the decomp's UI screens gets its layout."""
    table = tomllib.loads((repo / "patch" / game / "overlay.toml").read_text())
    layouts = table["layouts"]
    placements = [{"layout": table["outside"]} for _ in range(d.define("UI_SCREEN_COUNT"))]
    for name, entry in table["screens"].items():
        placements[d.define(name)] = {"layout": entry} if isinstance(entry, str) else entry
    for used in [table["outside"]] + [name for entry in placements for name in entry.values()]:
        if used not in layouts and used != "split":
            raise SystemExit(f"patch/{game}/overlay.toml names a layout that is not there: {used}")
    return {"screen_layouts": layouts, "screen_placements": tuple(placements), "screen_outside": table["outside"]}


def _guest_address(d: symbols.Decomp, terms: list[str | int]) -> int:
    """The sum of a hud.toml address: a global's name, `Struct.field` offsets, defines and numbers."""
    total = 0
    for term in terms:
        if isinstance(term, int):
            total += term
        elif "." in term:
            total += symbols.offset(d, term)
        elif term in d.defines:
            total += d.define(term)
        else:
            total += symbols.global_(d, term)
    return total


def _hud(repo: Path, game: str, d: symbols.Decomp) -> dict[str, object] | None:
    """hud.toml with every `{ at = [...], bytes = n }` resolved to `{ address, bytes }`."""
    path = repo / "patch" / game / "hud.toml"
    if not path.exists():
        return None
    table = tomllib.loads(path.read_text())
    elements = [{key: {"address": _guest_address(d, value["at"]), "bytes": value["bytes"]}
                 if isinstance(value, dict) else value for key, value in element.items()}
                for element in table["element"]]
    return {"icons": HUD_ICONS, "elements": elements}


# The folder of item icons beside the installed patch: <item id>.png.
HUD_ICONS = "z3d-hud-icons"


def _mm3d(repo: Path, d: symbols.Decomp) -> Layout:
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
        player=_player_list_head(d),
        actor_position=symbols.offset(d, "Actor.world"),
        actor_previous=symbols.offset(d, "Actor.prevPos"),
        actor_home=symbols.offset(d, "Actor.home"),
        camera_at=symbols.offset(d, "PlayState.viewAt"),
        camera_eye=symbols.offset(d, "PlayState.viewEye"),
        active_screen=symbols.global_(d, "gUiScreenContext") + symbols.offset(d, "UiScreenContext.activeScreen"),
        screens=symbols.global_(d, "gUiScreens"),
        **_overlay(repo, "mm3d", d),
        hud=_hud(repo, "mm3d", d),
        hud_visibility=symbols.global_(d, "gCommonData") + symbols.offset(d, "CommonData.hudVisibility"),
        hud_hidden=(d.define("HUD_VISIBILITY_NONE"), d.define("HUD_VISIBILITY_NONE_ALT")),
    )


_LAYOUTS = {"oot3d": _oot3d, "mm3d": _mm3d}


def layout(repo: Path, game: str) -> Layout:
    return _LAYOUTS[game](repo, symbols.load(repo, game))


TRANSITION_START = 20
