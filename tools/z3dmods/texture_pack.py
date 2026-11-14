"""Find a game texture's replacement in an Azahar custom texture pack by Azahar's own hash."""

from __future__ import annotations

import json
from pathlib import Path

from PIL import Image

from z3dmods import pica_texture

CONFIG = "pack.json"


class TexturePack:
    def __init__(self, root: Path) -> None:
        self._root = root
        self._files: dict[str, Path] | None = None
        config = root / CONFIG
        # Azahar names a pack without a config by the decoded texture.
        self.new_hash = config.exists() and bool(json.loads(config.read_text())["options"]["use_new_hash"])

    def _index(self) -> dict[str, Path]:
        if self._files is None:
            self._files = {path.name: path for path in self._root.rglob("tex1_*_mip0.png")}
        return self._files

    def name(self, image: Image.Image, data: bytes, gl_format: int) -> str:
        """The file name this pack gives the replacement of a texture: `data`, decoded as `image`."""
        return pica_texture.pack_name(image, data, gl_format, self.new_hash)

    def replacement(self, image: Image.Image, data: bytes, gl_format: int) -> Image.Image | None:
        """The pack's image for a game texture, or None when the pack has none."""
        if gl_format not in pica_texture.PACK_FORMATS:
            return None
        path = self._index().get(self.name(image, data, gl_format))
        if path is None:
            return None
        with Image.open(path) as found:
            return found.convert("RGBA")
