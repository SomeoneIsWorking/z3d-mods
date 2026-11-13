"""Stepped geometry for OoT3D's stair ramps.

The game draws stairs as smooth ramps with steps painted on (textures named *kaidan*, "stairs").
Each ramp is cut where its texture paints a step edge and folded into vertical risers and flat
treads that straddle the ramp by half a step, so the painted shading lands on the matching face.
Collision is untouched: Link still walks the ramp.
"""

from __future__ import annotations

import cmath
import math
import re
from dataclasses import dataclass

from PIL import Image

from z3dmods import cmb, etc1, zsi
from z3dmods.texture_pack import TexturePack, decoded

STAIR_TEXTURE = "kaidan"
# Faces flatter than this are landings, steeper ones are walls or modelled risers; both keep
# their shape.
MIN_SLOPE = math.radians(10)
MAX_SLOPE = math.radians(60)
PROFILE_BINS = 64
KEY = 1e-3
# Normals are stored as signed bytes.
NORMAL_STEPS = 127
EPSILON = 1e-9
ROOM_FILE = re.compile(r"^scene/.+_\d+_info\.zsi$")


@dataclass(frozen=True)
class Profile:
    """Where a stair texture paints its steps, in steps along v going uphill."""

    steps_per_tile: int
    # Phase of the bottom of a riser (the crease with the tread below), in steps.
    crease: float
    # Fraction of a step the riser takes.
    riser: float


def profile(image: Image.Image) -> Profile:
    """Find the painted steps: the strongest row period, its sharp light-to-dark edge (the nose,
    lit tread over shaded riser) and its darkest row (the crease).

    Rows run downhill as v falls; uphill a step reads crease, riser, nose, tread.
    """
    gray = image.convert("L")
    width, height = gray.size
    pixels = gray.tobytes()
    lum = [sum(pixels[r * width:(r + 1) * width]) / width for r in range(height)]
    mean = sum(lum) / height
    spectrum = [abs(sum((l - mean) * cmath.exp(-2j * math.pi * k * r / height) for r, l in enumerate(lum)))
                for k in range(2, height // 4)]
    steps = 2 + spectrum.index(max(spectrum))
    period = height / steps
    bins = [0.0] * PROFILE_BINS
    weight = [0] * PROFILE_BINS
    for r, l in enumerate(lum):
        b = int(((r + 0.5) / period % 1) * PROFILE_BINS)
        bins[b] += l
        weight[b] += 1
    # Rows can leave bins empty when a step is only a few rows tall; they take the previous bin.
    first = next(b for b in range(PROFILE_BINS) if weight[b])
    folded = [0.0] * PROFILE_BINS
    last = bins[first] / weight[first]
    for i in range(PROFILE_BINS):
        b = (first + i) % PROFILE_BINS
        last = bins[b] / weight[b] if weight[b] else last
        folded[b] = last
    nose = max(range(PROFILE_BINS), key=lambda b: folded[b] - folded[(b + 1) % PROFILE_BINS])
    crease = min(range(PROFILE_BINS), key=lambda b: folded[b])
    # Row phase to uphill step phase: t = -row / period.
    nose_t = -((nose + 1) / PROFILE_BINS) % 1
    crease_t = -((crease + 0.5) / PROFILE_BINS) % 1
    return Profile(steps, crease_t, (nose_t - crease_t) % 1)


@dataclass
class Vertex:
    position: tuple[float, float, float]
    # The shape's other per-vertex attributes, flattened; interpolated along cuts.
    extra: tuple[float, ...]
    uv: tuple[float, float]
    # Change of position per unit of v along the ramp's steepest v ascent; zero on landings.
    along: tuple[float, float, float]


def _lerp(a: tuple[float, ...], b: tuple[float, ...], f: float) -> tuple[float, ...]:
    return tuple(x + (y - x) * f for x, y in zip(a, b))


def _mix(a: Vertex, b: Vertex, f: float) -> Vertex:
    return Vertex(_lerp(a.position, b.position, f), _lerp(a.extra, b.extra, f), _lerp(a.uv, b.uv, f),
                  _lerp(a.along, b.along, f))


def _sub(a, b):
    return tuple(x - y for x, y in zip(a, b))


def _dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def _cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def _unit(a):
    length = math.sqrt(_dot(a, a))
    return tuple(x / length for x in a) if length > EPSILON else (0.0, 1.0, 0.0)


def _along(p: list[tuple[float, float, float]], v: list[float]) -> tuple[float, float, float] | None:
    """dp/dv up the triangle's v gradient, or None for a landing or a degenerate face."""
    e1, e2 = _sub(p[1], p[0]), _sub(p[2], p[0])
    normal = _unit(_cross(e1, e2))
    if not math.cos(MAX_SLOPE) <= abs(normal[1]) <= math.cos(MIN_SLOPE):
        return None
    d1, d2 = v[1] - v[0], v[2] - v[0]
    a11, a12, a22 = _dot(e1, e1), _dot(e1, e2), _dot(e2, e2)
    det = a11 * a22 - a12 * a12
    if abs(det) < EPSILON:
        return None
    a = (d1 * a22 - d2 * a12) / det
    b = (d2 * a11 - d1 * a12) / det
    g = tuple(a * x + b * y for x, y in zip(e1, e2))
    g2 = _dot(g, g)
    if g2 < EPSILON:
        return None
    return tuple(x / g2 for x in g)


def _key(position: tuple[float, ...]) -> tuple[int, ...]:
    return tuple(round(x / KEY) for x in position)


class Stairs:
    """One shape's ramp triangles turned into steps."""

    def __init__(self, positions, extras, uvs, indices: list[int], steps: Profile) -> None:
        self._profile = steps
        triangles = [indices[i:i + 3] for i in range(0, len(indices) - 2, 3)]
        sums: dict[tuple[int, ...], list[float]] = {}
        for tri in triangles:
            d = _along([positions[i] for i in tri], [uvs[i][1] for i in tri])
            if d is None:
                continue
            for i in tri:
                total = sums.setdefault(_key(positions[i]), [0.0, 0.0, 0.0, 0.0])
                for axis in range(3):
                    total[axis] += d[axis]
                total[3] += 1
        along = {k: (s[0] / s[3], s[1] / s[3], s[2] / s[3]) for k, s in sums.items()}

        def vertex(i: int) -> Vertex:
            return Vertex(positions[i], extras[i], uvs[i], along.get(_key(positions[i]), (0.0, 0.0, 0.0)))

        self._triangles = [[vertex(i) for i in tri] for tri in triangles]
        # Edges no other triangle shares, with the triangle's third corner to tell inside from out.
        edges: dict[frozenset, list[tuple[Vertex, Vertex, Vertex]]] = {}
        for tri in self._triangles:
            for a, b, c in ((tri[0], tri[1], tri[2]), (tri[1], tri[2], tri[0]), (tri[2], tri[0], tri[1])):
                edges.setdefault(frozenset((_key(a.position), _key(b.position))), []).append((a, b, c))
        self._boundary = [found[0] for found in edges.values() if len(found) == 1]

    def _t(self, vertex: Vertex) -> float:
        return vertex.uv[1] * self._profile.steps_per_tile

    def _levels(self, low: float, high: float) -> list[float]:
        p = self._profile
        out = []
        k = math.floor(low - p.crease) - 1
        while True:
            for level in (k + p.crease, k + p.crease + p.riser):
                if low + EPSILON < level < high - EPSILON:
                    out.append(level)
            if k + p.crease > high:
                return out
            k += 1

    def _split(self, polygon: list[Vertex]) -> list[list[Vertex]]:
        ts = [self._t(v) for v in polygon]
        pieces = []
        rest = polygon
        for level in self._levels(min(ts), max(ts)):
            below, above = [], []
            for i, a in enumerate(rest):
                b = rest[(i + 1) % len(rest)]
                ta, tb = self._t(a), self._t(b)
                if ta <= level + EPSILON:
                    below.append(a)
                if ta >= level - EPSILON:
                    above.append(a)
                if (ta - level) * (tb - level) < 0:
                    cut = _mix(a, b, (level - ta) / (tb - ta))
                    below.append(cut)
                    above.append(cut)
            if len(below) >= 3:
                pieces.append(below)
            rest = above
            if len(rest) < 3:
                break
        if len(rest) >= 3:
            pieces.append(rest)
        return pieces

    def _place(self, vertex: Vertex) -> tuple[float, float, float]:
        """Fold a ramp point onto the staircase."""
        p = self._profile
        t = self._t(vertex)
        j = math.floor(t - p.crease)
        u = t - p.crease - j
        nose = p.crease + j + p.riser / 2
        if u < p.riser:
            across, up = nose, nose - 0.5 + u / p.riser
        else:
            across, up = nose + (u - p.riser) / (1 - p.riser), nose + 0.5
        d = vertex.along
        scale = 1 / p.steps_per_tile
        return (vertex.position[0] + d[0] * (across - t) * scale,
                vertex.position[1] + d[1] * (up - t) * scale,
                vertex.position[2] + d[2] * (across - t) * scale)

    def _boundary_chain(self, a: Vertex, b: Vertex) -> list[Vertex]:
        ta, tb = self._t(a), self._t(b)
        if abs(tb - ta) < EPSILON:
            return [a, b]
        levels = self._levels(min(ta, tb), max(ta, tb))
        cuts = [_mix(a, b, (level - ta) / (tb - ta)) for level in levels]
        if tb < ta:
            cuts.reverse()
        return [a, *cuts, b]

    def triangles(self) -> list[tuple[tuple[tuple[float, ...], tuple[float, ...]], ...]]:
        """Output triangles as three (position, extra) corners, flat shaded by the caller."""
        out = []
        for tri in self._triangles:
            for piece in self._split(tri):
                corners = [(self._place(v), v.extra) for v in piece]
                for i in range(1, len(corners) - 1):
                    out.append((corners[0], corners[i], corners[i + 1]))
        # Skirts close the gap between the ramp's open edges and the steps, facing outward.
        for a, b, inside in self._boundary:
            chain = self._boundary_chain(a, b)
            for c, d in zip(chain, chain[1:]):
                low_c, low_d = (c.position, c.extra), (d.position, d.extra)
                high_c, high_d = (self._place(c), c.extra), (self._place(d), d.extra)
                quad = (low_c, low_d, high_d, high_c)
                normal = _cross(_sub(low_d[0], low_c[0]), _sub(high_c[0], low_c[0]))
                middle = _lerp(c.position, d.position, 0.5)
                if _dot(normal, _sub(inside.position, middle)) > 0:
                    quad = (low_c, high_c, high_d, low_d)
                out.append((quad[0], quad[1], quad[2]))
                out.append((quad[0], quad[2], quad[3]))
        return out


def _flat(triangles, extras: list[str]) -> tuple[dict[str, list[tuple[float, ...]]], list[int]]:
    """Deduplicated vertices with face normals, and the index list.

    `extras` names the attributes packed into each corner's extra tuple, in order.
    """
    columns: dict[str, list[tuple[float, ...]]] = {"position": [], "normal": []}
    columns.update({name: [] for name in extras})
    seen: dict[tuple, int] = {}
    indices = []
    for tri in triangles:
        positions = [c[0] for c in tri]
        n = _cross(_sub(positions[1], positions[0]), _sub(positions[2], positions[0]))
        if math.sqrt(_dot(n, n)) < 1e-4:
            continue
        normal = _unit(n)
        for position, extra in tri:
            key = (_key(position), tuple(round(x * NORMAL_STEPS) for x in normal), _key(extra))
            if key not in seen:
                seen[key] = len(columns["position"])
                columns["position"].append(position)
                columns["normal"].append(normal)
                at = 0
                for name in extras:
                    width = cmb.WIDTHS[name]
                    columns[name].append(extra[at:at + width])
                    at += width
            indices.append(seen[key])
    return columns, indices


def texture_image(model: cmb.Cmb, texture: cmb.Texture, pack: TexturePack | None) -> Image.Image:
    """The texture as the player sees it: the pack's replacement when it has one."""
    if texture.format not in (etc1.ETC1, etc1.ETC1A4):
        raise ValueError(f"{texture.name}: format 0x{texture.format:04X} is not ETC1")
    image = decoded(etc1.decode(model.texture_data(texture), texture.width, texture.height,
                                texture.format == etc1.ETC1A4))
    found = pack.replacement(image, texture.format) if pack else None
    return found or image


def rebuild(model: cmb.Cmb, pack: TexturePack | None) -> tuple[bytes, list[str]]:
    """The model with every stair-textured shape stepped, and the textures it stepped."""
    data = model.data
    stepped = []
    done: set[int] = set()
    for mesh in model.meshes:
        slot = model.materials[mesh.material][0]
        if slot < 0 or STAIR_TEXTURE not in model.textures[slot].name or mesh.shape in done:
            continue
        done.add(mesh.shape)
        current = cmb.Cmb.parse(data)
        texture = current.textures[slot]
        shape = current.shapes[mesh.shape]
        arrays = current.arrays(shape)
        if not {"position", "normal", "tex0"} <= set(arrays):
            raise ValueError(f"{texture.name}: shape {mesh.shape} has arrays {arrays}")
        extras = [name for name in arrays if name not in ("position", "normal")]
        columns = [current.vertices(shape, name) for name in extras]
        # Slices hold the vertices of every later shape too; the shape's own are the shortest run.
        count = min(len(column) for column in [current.vertices(shape, "position"), *columns])
        positions = current.vertices(shape, "position")[:count]
        packed = [tuple(v for column in columns for v in column[i]) for i in range(count)]
        stairs = Stairs(positions, packed, current.vertices(shape, "tex0"), current.indices(shape),
                        profile(texture_image(current, texture, pack)))
        vertices, indices = _flat(stairs.triangles(), extras)
        data = current.with_shape(mesh.shape, vertices, indices)
        stepped.append(texture.name)
    return data, stepped


def room(data: bytes, pack: TexturePack | None) -> tuple[bytes, list[str]]:
    """A room file with its stair ramps stepped; unchanged when it has none."""
    room_ = zsi.Room.parse(data)
    stepped: list[str] = []
    for model in reversed(room_.models()):
        cmb_bytes, names = rebuild(cmb.Cmb.parse(data[model:]), pack)
        if names:
            data = zsi.Room.parse(data).with_model(model, cmb_bytes)
            stepped.extend(names)
    return data, stepped


def is_room(path: str) -> bool:
    return bool(ROOM_FILE.match(path))
