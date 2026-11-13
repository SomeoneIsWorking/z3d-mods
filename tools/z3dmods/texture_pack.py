"""Find a game texture's replacement in an Azahar custom texture pack by Azahar's own hash."""

from __future__ import annotations

from pathlib import Path

from PIL import Image

from z3dmods import pica_texture


class TexturePack:
    def __init__(self, root: Path) -> None:
        self._root = root
        self._files: dict[str, Path] | None = None

    def _index(self) -> dict[str, Path]:
        if self._files is None:
            self._files = {path.name: path for path in self._root.rglob("tex1_*_mip0.png")}
        return self._files

    def replacement(self, image: Image.Image, gl_format: int) -> Image.Image | None:
        """The pack's image for a game texture, or None when the pack has none."""
        if gl_format not in pica_texture.PACK_FORMATS:
            return None
        path = self._index().get(pica_texture.pack_name(image, gl_format))
        if path is None:
            return None
        with Image.open(path) as found:
            return found.convert("RGBA")
