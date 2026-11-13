"""The Azahar libretro core's options for a game's set: the one place the app's runs get them."""

from __future__ import annotations

from z3dmods.install import GameSet

BASE = {
    "citra_graphics_api": "Vulkan",
    "citra_use_libretro_save_path": "LibRetro Default",
}


def for_set(game: GameSet, resolution: int, extra: dict[str, str] | None = None) -> dict[str, str]:
    """`resolution` is the internal render scale (1 is the 3DS's own 400x240 top screen)."""
    options = dict(BASE, citra_layout_option=game.screen_layout,
                   citra_custom_textures="enabled" if game.textures is not None else "disabled",
                   citra_resolution_factor=str(resolution))
    options.update(extra or {})
    return options
