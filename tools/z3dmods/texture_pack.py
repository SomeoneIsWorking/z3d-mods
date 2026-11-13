"""Find a game texture's replacement in an Azahar custom texture pack by its look.

Packs name files `tex1_<w>x<h>_<hash>_<format>_mip0.png` after Azahar's hash of the decoded
texture; matching a downscaled copy against the game's own texture avoids reproducing that hash.
"""

from __future__ import annotations

from pathlib import Path

from PIL import Image

from z3dmods import etc1

# Azahar's VideoCore::PixelFormat numbers, as packs spell them in file names.
PACK_FORMATS = {etc1.ETC1: 12, etc1.ETC1A4: 13}
THUMB = (16, 16)
# Mean absolute RGB difference of the thumbnails; a match must also beat the runner-up by this factor.
MATCH = 10.0
UNIQUE = 2.0


class TexturePack:
    def __init__(self, root: Path) -> None:
        self._root = root
        self._thumbs: dict[tuple[int, int, int], list[tuple[Path, bytes]]] = {}

    def _candidates(self, width: int, height: int, pack_format: int) -> list[tuple[Path, bytes]]:
        key = (width, height, pack_format)
        if key not in self._thumbs:
            found = []
            for path in sorted(self._root.rglob(f"tex1_{width}x{height}_*_{pack_format}_mip0.png")):
                with Image.open(path) as image:
                    found.append((path, image.convert("RGB").resize(THUMB, Image.BOX).tobytes()))
            self._thumbs[key] = found
        return self._thumbs[key]

    def replacement(self, image: Image.Image, gl_format: int) -> Image.Image | None:
        """The pack's image for a game texture, or None when the pack has none."""
        if gl_format not in PACK_FORMATS:
            return None
        thumb = image.convert("RGB").resize(THUMB, Image.BOX).tobytes()
        scored = sorted((sum(abs(a - b) for a, b in zip(thumb, other)) / len(thumb), path)
                        for path, other in self._candidates(*image.size, PACK_FORMATS[gl_format]))
        if not scored or scored[0][0] > MATCH:
            return None
        if len(scored) > 1 and scored[1][0] < scored[0][0] * UNIQUE:
            return None
        with Image.open(scored[0][1]) as found:
            return found.convert("RGB")


def decoded(rows: list[list[tuple[int, int, int, int]]]) -> Image.Image:
    image = Image.new("RGB", (len(rows[0]), len(rows)))
    image.putdata([px[:3] for row in rows for px in row])
    return image
