"""Guest RAM layout the maintainer tools read, per title, resolved from the game's decomp."""

from __future__ import annotations

import tomllib
from dataclasses import dataclass
from pathlib import Path

from z3dmods import symbols, system_config


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
    # MM3D only: the entry a scene load starts from in place of its entrance (decomp/mm3d
    # docs/save.md, "Respawn"): the flag's address, the first entry's, and the entry's fields.
    respawn_flag: int | None = None
    respawn: int | None = None
    respawn_yaw: int | None = None
    respawn_entrance: int | None = None
    respawn_room: int | None = None
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
    # aim.toml as the app reads it (app/src/mouse_aim.hpp): guest addresses and offsets.
    aim: dict[str, int] | None = None
    # interpolation.toml as the app reads it (app/src/frame_interpolation.hpp): the draw section.
    interpolation: dict[str, int | list[int]] | None = None
    # CommonData.hudVisibility and its values that leave the main touch screen blank or dimmed.
    hud_visibility: int | None = None
    hud_hidden: tuple[int, ...] | None = None


# ACTORCAT_PLAYER, the player's actor list.
PLAYER_CATEGORY = 2


def _player_list_head(d: symbols.Decomp) -> int:
    """PlayState offset of the player category's first actor."""
    return (symbols.offset(d, "PlayState.actorCtx") + symbols.offset(d, "ActorContext.actorLists")
            + PLAYER_CATEGORY * symbols.size(d, "ActorListEntry") + symbols.offset(d, "ActorListEntry.head"))


def _oot3d(repo: Path, d: symbols.Decomp) -> Layout:
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
        hud=_hud(repo, "oot3d", d),
        aim=_aim(repo, "oot3d", d),
        interpolation=_interpolation(repo, "oot3d", d),
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


def _defined(d: symbols.Decomp, value: object) -> object:
    """A number, one of the decomp's defines by name, or a list of either."""
    if isinstance(value, list):
        return [_defined(d, entry) for entry in value]
    return d.define(value) if isinstance(value, str) else _resolved(d, value)


def _resolved(d: symbols.Decomp, value: object) -> object:
    """hud.toml's `{ at = [...], through = "global" }` as `{ address, pointer }`, wherever it sits
    (`of = "player"`: an offset in the player actor, `pointer` the play state's and `hop` the actor's in it);
    a `mask`, `shift`, the `equals` of a condition (one value or a list), or an `item` and
    `upgrade` may be one of the decomp's defines."""
    if isinstance(value, list):
        return [_resolved(d, entry) for entry in value]
    if not isinstance(value, dict):
        return value
    out = {key: _defined(d, entry) if key in DEFINED else _resolved(d, entry)
           for key, entry in value.items() if key not in ("at", "through", "of")}
    if "at" in value:
        out["address"] = _guest_address(d, value["at"])
    if "through" in value:
        out["pointer"] = symbols.global_(d, value["through"])
    if "of" in value:
        if value["of"] != "player":
            raise SystemExit(f"hud.toml: `of` is \"player\", not {value['of']}")
        out["pointer"] = symbols.global_(d, "gPlayState")
        out["hop"] = _player_list_head(d)
    return out


def _hud(repo: Path, game: str, d: symbols.Decomp) -> dict[str, object] | None:
    """hud.toml as the app reads it: every guest value resolved to its address, and the system
    languages the A button's words are cut for (`words`, the first for any other language)."""
    path = repo / "patch" / game / "hud.toml"
    if not path.exists():
        return None
    table = tomllib.loads(path.read_text())
    shared = {key: d.define(table[key]) if isinstance(table[key], str) else _resolved(d, table[key])
              for key in HUD_SHARED if key in table}
    words = [system_config.LANGUAGES[language] for language in table["words"]["languages"]] if "words" in table else []
    return {"icons": HUD_ICONS, "words": words, "show": _resolved(d, table.get("show", [])),
            "elements": _resolved(d, table["element"])} | shared


def _aim(repo: Path, game: str, d: symbols.Decomp) -> dict[str, int]:
    """aim.toml resolved, with the play state's pointer and the player's in it."""
    table = tomllib.loads((repo / "patch" / game / "aim.toml").read_text())
    return {key: _guest_address(d, terms) for key, terms in table.items()} | {
        "play": symbols.global_(d, "gPlayState"), "player": _player_list_head(d)}


def _interpolation(repo: Path, game: str, d: symbols.Decomp) -> dict[str, int | list[int]] | None:
    """interpolation.toml resolved: the sites are a function and a define of its offset (a site list, several), the functions a function, the rest
    addresses and offsets as in aim.toml, with the play state's pointer, a GX request's size and the sizes the tracks the table names use."""
    path = repo / "patch" / game / "interpolation.toml"
    if not path.exists():
        return None
    table = tomllib.loads(path.read_text())
    sites = ("open_site", "close_site")
    site_lists = ("skeleton_draw_calls", "material_step_calls", "material_lead_calls")
    functions = ("fade_draw", "material_update")
    sizes = {key: symbols.size(d, struct) for key, (anchor, struct) in INTERPOLATION_SIZES.items() if anchor in table}
    return {key: symbols.function(d, table[key][0]) + d.define(table[key][1]) for key in sites if key in table} | {
        key: [symbols.function(d, site[0]) + d.define(site[1]) for site in table[key]] for key in site_lists if key in table} | {
        key: symbols.function(d, table[key][0]) for key in functions if key in table} | {
        key: _guest_address(d, terms) for key, terms in table.items()
        if key not in sites + site_lists + functions} | {
        "play": symbols.global_(d, "gPlayState"), "request_size": symbols.size(d, "GxRequest")} | sizes


# The struct sizes the app's tracks step by: each key, the table key that names its track, its struct.
INTERPOLATION_SIZES = {
    "actor_list_size": ("actor_lists", "ActorListEntry"), "joint_size": ("skeleton_draw_calls", "SkelJoint"),
    "effect_size": ("effects", "ObjectKankyoEffect"),
    "camera_setting_size": ("door_camera_function", "CameraSetting"),
    "camera_mode_size": ("door_camera_function", "CameraMode")}


# hud.toml's tables every button shares: the counts and when each is full, the statuses, the
# minigames' lent items and the songs' colours.
HUD_SHARED = ("ammo", "ammo_index", "ammo_full", "upgrades", "upgrade_masks", "upgrade_shifts",
              "upgrade_capacities", "upgrade_levels", "status", "disabled", "status_lanes", "lent", "song_colours")

# The keys whose string value is one of the decomp's defines.
DEFINED = ("mask", "shift", "equals", "item", "upgrade", "full")

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
        camera_at=symbols.offset(d, "PlayState.view") + symbols.offset(d, "View.at"),
        camera_eye=symbols.offset(d, "PlayState.view") + symbols.offset(d, "View.eye"),
        respawn_flag=symbols.global_(d, "gCommonData") + symbols.offset(d, "CommonData.respawnFlag"),
        respawn=symbols.global_(d, "gCommonData") + symbols.offset(d, "CommonData.respawn"),
        respawn_yaw=symbols.offset(d, "RespawnData.yaw"),
        respawn_entrance=symbols.offset(d, "RespawnData.entrance"),
        respawn_room=symbols.offset(d, "RespawnData.room"),
        active_screen=symbols.global_(d, "gUiScreenContext") + symbols.offset(d, "UiScreenContext.activeScreen"),
        screens=symbols.global_(d, "gUiScreens"),
        **_overlay(repo, "mm3d", d),
        hud=_hud(repo, "mm3d", d),
        aim=_aim(repo, "mm3d", d),
        interpolation=_interpolation(repo, "mm3d", d),
        hud_visibility=symbols.global_(d, "gCommonData") + symbols.offset(d, "CommonData.hudVisibility"),
        hud_hidden=(d.define("HUD_VISIBILITY_NONE"), d.define("HUD_VISIBILITY_NONE_ALT")),
    )


_LAYOUTS = {"oot3d": _oot3d, "mm3d": _mm3d}


def layout(repo: Path, game: str) -> Layout:
    return _LAYOUTS[game](repo, symbols.load(repo, game))


TRANSITION_START = 20
