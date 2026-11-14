"""Stepped geometry for OoT3D's and MM3D's stair ramps.

The game draws stairs as smooth ramps with steps painted on (textures named *kaidan*, "stairs").
The ramp is the stairs' pitch line, the line through the step noses, as on real stairs: each ramp
is cut where its texture paints a step edge and folded into vertical risers and flat treads below
it, with each nose on the ramp where the paint has it. The curbs the texture paints along its sides
stay flat on the ramp, and the steps sit between them; side faces close every step edge up to the
ramp. Nothing rises above the ramp, so the steps stay inside the walls that meet it. The scene's
collision is cut at each stepped ramp's outline and folded the same way inside it, so Link walks
the treads.
"""

from __future__ import annotations

import cmath
import itertools
import math
import re
from dataclasses import dataclass, replace

from PIL import Image

from z3dmods import cmb, pica_texture, zsi
from z3dmods.texture_pack import TexturePack

# Faces flatter than this are landings, steeper ones are walls or modelled risers; both keep
# their shape.
MIN_SLOPE = math.radians(10)
MAX_SLOPE = math.radians(60)
PROFILE_BINS = 64
# A curb column's step-period strength is below this share of the strong columns', and a curb is
# at most this share of the texture wide.
CURB_STRENGTH = 0.4
MAX_CURB = 0.25
KEY = 1e-3
# Normals are stored as signed bytes.
NORMAL_STEPS = 127
EPSILON = 1e-9
# OoT3D keeps them in scene/, MM3D in scenes/.
ROOM_FILE = re.compile(r"^(scenes?)/(.+)_\d+_info\.zsi$")
# A collision triangle's part over a drawn ramp is on it when it is this close above or below
# (N64 units; collision vertices are integers).
ON_RAMP = 4.0
# A closing face whose normal is flatter than this is upright.
UPRIGHT = 0.05
# A collision edge this close to a ramp's edge is that edge: no sliver is cut off between them.
SAME_EDGE = 1.0
# Painted steps rise between these (N64 units). Link walks up a 16 unit step (z2_kindan2) and
# stops at a 25 unit one; lower than the first, the texture's rows run up the slope, not across.
MIN_RISE = 3.0
MAX_RISE = 20.0
# Edges whose lines pass this close to each other lie on one line.
LINE = 1e-2
# The steps reach this far below the lowest tread's height: integer corners, and the feet on it.
UNDER_TREAD = 1.0


@dataclass(frozen=True)
class Profile:
    """Where a stair texture paints its steps, in steps along v going uphill."""

    steps_per_tile: int
    # Phase of the bottom of a riser (the crease with the tread below), in steps.
    crease: float
    # Fraction of a step the riser takes.
    riser: float
    # The steps lie between these u (fractions of the tile); outside are the curbs painted along
    # the sides, which stay flat on the ramp.
    first: float = 0.0
    last: float = 1.0


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
    first, last = _curbs(gray, steps)
    return Profile(steps, crease_t, (nose_t - crease_t) % 1, first, last)


def _curbs(gray: Image.Image, steps: int) -> tuple[float, float]:
    """The u range with painted steps: edge columns where the step period is weak are curbs."""
    width, height = gray.size
    pixels = gray.tobytes()
    strength = []
    for x in range(width):
        column = pixels[x::width]
        mean = sum(column) / height
        strength.append(abs(sum((v - mean) * cmath.exp(-2j * math.pi * steps * y / height)
                                for y, v in enumerate(column))))
    strong = sorted(strength)[width * 3 // 4] * CURB_STRENGTH
    first = 0
    while first < width and strength[first] < strong:
        first += 1
    last = width
    while last > first and strength[last - 1] < strong:
        last -= 1
    first = first if first <= width * MAX_CURB else 0
    last = last if width - last <= width * MAX_CURB else width
    return first / width, last / width


@dataclass
class Vertex:
    position: tuple[float, float, float]
    # The shape's other per-vertex attributes, flattened; interpolated along cuts.
    extra: tuple[float, ...]
    uv: tuple[float, float]
    # Change of position per unit of v along the ramp's steepest v ascent; zero on landings.
    along: tuple[float, float, float]
    # The height of its flight's foot: no step folds below the floor the flight starts from.
    foot: float


def _lerp(a: tuple[float, ...], b: tuple[float, ...], f: float) -> tuple[float, ...]:
    return tuple(x + (y - x) * f for x, y in zip(a, b))


def _mix(a: Vertex, b: Vertex, f: float) -> Vertex:
    return Vertex(_lerp(a.position, b.position, f), _lerp(a.extra, b.extra, f), _lerp(a.uv, b.uv, f),
                  _lerp(a.along, b.along, f), max(a.foot, b.foot))


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


def _climb(p: list[tuple[float, float, float]], v: list[float], steps: Profile) -> tuple[float, float, float] | None:
    """`_along` for a triangle whose painted steps are stairs to walk up, else None."""
    d = _along(p, v)
    if d is None or not MIN_RISE <= abs(d[1]) / steps.steps_per_tile <= MAX_RISE:
        return None
    return d


def _key(position: tuple[float, ...]) -> tuple[int, ...]:
    return tuple(round(x / KEY) for x in position)


class Stairs:
    """One shape's ramp triangles turned into steps."""

    def __init__(self, positions, extras, uvs, indices: list[int], steps: Profile) -> None:
        self._profile = steps
        triangles = [indices[i:i + 3] for i in range(0, len(indices) - 2, 3)]
        sums: dict[tuple[int, ...], list[float]] = {}
        # Ramp triangles that share a corner are one flight.
        flight: dict[tuple[int, ...], tuple[int, ...]] = {}

        def root(key: tuple[int, ...]) -> tuple[int, ...]:
            while flight.setdefault(key, key) != key:
                flight[key] = key = flight[flight[key]]
            return key

        for tri in triangles:
            d = _climb([positions[i] for i in tri], [uvs[i][1] for i in tri], steps)
            if d is None:
                continue
            for i in tri:
                total = sums.setdefault(_key(positions[i]), [0.0, 0.0, 0.0, 0.0])
                for axis in range(3):
                    total[axis] += d[axis]
                total[3] += 1
                flight[root(_key(positions[i]))] = root(_key(positions[tri[0]]))
        along = {k: (s[0] / s[3], s[1] / s[3], s[2] / s[3]) for k, s in sums.items()}
        foot: dict[tuple[int, ...], float] = {}
        for i in {i for tri in triangles for i in tri}:
            key = _key(positions[i])
            if key in flight:
                foot[root(key)] = min(foot.get(root(key), math.inf), positions[i][1])

        def vertex(i: int) -> Vertex:
            key = _key(positions[i])
            return Vertex(positions[i], extras[i], uvs[i], along.get(key, (0.0, 0.0, 0.0)),
                          foot[root(key)] if key in flight else -math.inf)

        self._triangles = [[vertex(i) for i in tri] for tri in triangles]

    def _t(self, vertex: Vertex) -> float:
        return vertex.uv[1] * self._profile.steps_per_tile

    def _u(self, vertex: Vertex) -> float:
        return vertex.uv[0]

    def _t_levels(self, low: float, high: float) -> list[float]:
        p = self._profile
        return _levels(low, high, (p.crease, p.crease + p.riser))

    def _u_levels(self, low: float, high: float) -> list[float]:
        p = self._profile
        return _levels(low, high, tuple(cut for cut in (p.first, p.last) if 0 < cut < 1))

    def _stepped(self, piece: list[Vertex]) -> bool:
        u = sum(self._u(v) for v in piece) / len(piece) % 1
        return self._profile.first <= u <= self._profile.last

    def _place(self, vertex: Vertex) -> tuple[float, float, float]:
        """Fold a ramp point onto the staircase: the ramp is the line through the noses."""
        p = self._profile
        t = self._t(vertex)
        j = math.floor(t - p.crease)
        u = t - p.crease - j
        nose = p.crease + j + p.riser
        if u < p.riser:
            across, up = nose, nose - 1 + u / p.riser
        else:
            across, up = nose + (u - p.riser) / (1 - p.riser), nose
        d = vertex.along
        scale = 1 / p.steps_per_tile
        return (vertex.position[0] + d[0] * (across - t) * scale,
                max(vertex.position[1] + d[1] * (up - t) * scale, vertex.foot),
                vertex.position[2] + d[2] * (across - t) * scale)

    def _fold(self, whole_curbs: bool = False) -> tuple[list, list[tuple]]:
        """The folded faces as (position, extra) corner triples, and the faces that close the steps
        wherever no other step continues them: up to the ramp along the curbs and the ramp's open
        edges, and between two ramps' steps where they meet out of phase."""
        out = []
        stepped = []
        for tri in self._triangles:
            for column in _split(tri, self._u, self._u_levels):
                # Cut like the steps beside it, a curb meets them corner to corner when drawn.
                pieces = [column] if whole_curbs and not self._stepped(column) else _split(column, self._t, self._t_levels)
                for piece in pieces:
                    folds = self._stepped(piece)
                    corners = [(self._place(v) if folds else v.position, v.extra) for v in piece]
                    out.extend((corners[0], corners[i], corners[i + 1]) for i in range(1, len(corners) - 1))
                    if folds:
                        stepped.append(piece)
        # Edges that lie on one line, as (start, end, corner, corner) along it: pieces cut at
        # different places share parts of an edge, not whole edges.
        lines: dict[tuple, list[tuple[float, float, Vertex, Vertex]]] = {}
        for piece in stepped:
            for a, b in zip(piece, piece[1:] + piece[:1]):
                d = _sub(b.position, a.position)
                length = math.sqrt(_dot(d, d))
                if length < KEY:
                    continue
                d = tuple(x / length for x in d)
                if d < (0.0, 0.0, 0.0):
                    a, b, d = b, a, tuple(-x for x in d)
                start = _dot(a.position, d)
                nearest = tuple(p - start * x for p, x in zip(a.position, d))
                line = (tuple(round(x / KEY) for x in d), tuple(round(x / LINE) for x in nearest))
                lines.setdefault(line, []).append((start, start + length, a, b))
        closing = []

        def close(quad: list[tuple[float, float, float]], extras: list[tuple[float, ...]]) -> None:
            corners = list(zip(quad, extras))
            closing.append((corners[0], corners[1], corners[2]))
            closing.append((corners[0], corners[2], corners[3]))

        # Two steps continue each other along an edge when they fold it to the same place. Ramps
        # that meet with their painted steps out of phase fold it to two: a face joins those.
        # An edge with no step beyond it is closed up to the ramp.
        for found in lines.values():
            cuts = sorted({s for start, end, _, _ in found for s in (start, end)})
            for low, high in itertools.pairwise(cuts):
                if high - low < KEY:
                    continue
                parts = [(_mix(a, b, (low - start) / (end - start)), _mix(a, b, (high - start) / (end - start)))
                         for start, end, a, b in found if start < low + KEY and end > high - KEY]
                if len(parts) == 1:
                    a, b = parts[0]
                    close([a.position, b.position, self._place(b), self._place(a)], [a.extra, b.extra, b.extra, a.extra])
                for (a, b), (c, d) in itertools.pairwise(parts):
                    quad = [self._place(a), self._place(b), self._place(d), self._place(c)]
                    if _key(quad[0]) != _key(quad[3]) or _key(quad[1]) != _key(quad[2]):
                        close(quad, [a.extra, b.extra, d.extra, c.extra])
        return out, closing

    def triangles(self) -> list[tuple[tuple[tuple[float, ...], tuple[float, ...]], ...]]:
        """The drawn faces, flat shaded by the caller; the closing faces are seen from either side."""
        out, closing = self._fold()
        return out + closing + [(a, c, b) for a, b, c in closing]

    def floor(self) -> list[tuple[tuple[tuple[float, ...], tuple[float, ...]], ...]]:
        """The faces to stand on and walk into. A step's plan moves up the ramp as it folds, so
        the closing faces that are not upright fill the plan it leaves at the ramp's ends; they
        face up. The upright ones are lower than the height the game checks walls at."""
        out, closing = self._fold(whole_curbs=True)
        for a, b, c in closing:
            normal = _cross(_sub(b[0], a[0]), _sub(c[0], a[0]))
            if abs(normal[1]) > UPRIGHT * math.sqrt(_dot(normal, normal)):
                out.append((a, b, c) if normal[1] > 0 else (a, c, b))
        return out


def _levels(low: float, high: float, phases: tuple[float, ...]) -> list[float]:
    """Every `k + phase` strictly between low and high, ascending."""
    out = []
    k = math.floor(low) - 1
    while k - 1 <= high:
        out.extend(k + phase for phase in phases if low + EPSILON < k + phase < high - EPSILON)
        k += 1
    return sorted(out)


def _split(polygon: list[Vertex], coordinate, levels) -> list[list[Vertex]]:
    """A convex polygon cut into slabs at each level of a coordinate linear across it."""
    values = [coordinate(v) for v in polygon]
    pieces = []
    rest = polygon
    for level in levels(min(values), max(values)):
        below, above = [], []
        for i, a in enumerate(rest):
            b = rest[(i + 1) % len(rest)]
            ca, cb = coordinate(a), coordinate(b)
            if ca <= level + EPSILON:
                below.append(a)
            if ca >= level - EPSILON:
                above.append(a)
            if (ca - level) * (cb - level) < 0:
                cut = _mix(a, b, (level - ca) / (cb - ca))
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
    data = model.texture_data(texture)
    image = pica_texture.decode(data, texture.width, texture.height, texture.format)
    found = pack.replacement(image, data, texture.format) if pack else None
    return found or image


def _stair_shapes(model: cmb.Cmb, named: str) -> list[tuple[int, int]]:
    """(shape, texture slot) of every shape whose texture name matches `named`, once each."""
    found: dict[int, int] = {}
    for mesh in model.meshes:
        slot = model.materials[mesh.material][0]
        if slot >= 0 and re.search(named, model.textures[slot].name, re.IGNORECASE):
            found.setdefault(mesh.shape, slot)
    return list(found.items())


@dataclass(frozen=True)
class Ramp:
    """One drawn ramp triangle, before stepping: corners, their texture coordinates, its steps."""

    positions: tuple[tuple[float, float, float], ...]
    uvs: tuple[tuple[float, float], ...]
    profile: Profile
    # The convex piece of the ramp this triangle's plane and texture frame hold for, seen from
    # above: the triangle itself, or it joined with its neighbours.
    outline: tuple[tuple[float, float, float], ...] = ()

    def edge(self) -> tuple[tuple[float, float, float], ...]:
        return self.outline or self.positions

    def continues(self, other: Ramp) -> bool:
        """Whether `other` lies in this triangle's plane with the same steps painted on it."""
        if other.profile != self.profile:
            return False
        for (x, y, z), uv in zip(other.positions, other.uvs):
            mine = self.uv(x, z)
            if abs(self.height(x, z) - y) > KEY or any(abs(math.remainder(a - b, 1)) > KEY for a, b in zip(mine, uv)):
                return False
        return True

    def _weights(self, x: float, z: float) -> tuple[float, float, float] | None:
        (ax, _, az), (bx, _, bz), (cx, _, cz) = self.positions
        det = (bz - cz) * (ax - cx) + (cx - bx) * (az - cz)
        if abs(det) < EPSILON:
            return None
        wa = ((bz - cz) * (x - cx) + (cx - bx) * (z - cz)) / det
        wb = ((cz - az) * (x - cx) + (ax - cx) * (z - cz)) / det
        return wa, wb, 1 - wa - wb

    def height(self, x: float, z: float) -> float | None:
        """The ramp plane's height over (x, z)."""
        w = self._weights(x, z)
        return None if w is None else sum(f * p[1] for f, p in zip(w, self.positions))

    def uv(self, x: float, z: float) -> tuple[float, float]:
        """The ramp's texture coordinates continued to (x, z)."""
        w = self._weights(x, z)
        return tuple(sum(f * uv[axis] for f, uv in zip(w, self.uvs)) for axis in range(2))

    def cut(self, polygon: list[tuple[float, float, float]]):
        """A convex polygon cut at this triangle's outline, seen from above: the part over the
        triangle (None when there is none) and the parts beside it."""
        p = self.edge()
        turn = _turn(p)
        over = polygon
        beside = []
        for i in range(len(p)):
            (ax, _, az), (bx, _, bz) = p[i], p[(i + 1) % len(p)]
            length = math.hypot(bx - ax, bz - az) * (1 if turn > 0 else -1)
            if abs(length) < EPSILON:
                return None, [polygon]
            inward = [((bx - ax) * (z - az) - (bz - az) * (x - ax)) / length for x, _, z in over]
            if max(inward) <= SAME_EDGE:
                return None, [polygon]
            if min(inward) >= -SAME_EDGE:
                continue
            kept, cut = [], []
            for j, a in enumerate(over):
                k = (j + 1) % len(over)
                if inward[j] >= 0:
                    kept.append(a)
                if inward[j] <= 0:
                    cut.append(a)
                if inward[j] * inward[k] < 0:
                    crossing = _lerp(a, over[k], inward[j] / (inward[j] - inward[k]))
                    kept.append(crossing)
                    cut.append(crossing)
            beside.append(cut)
            over = kept
        return over, beside

    def rise(self) -> float:
        """The height of one of its steps."""
        d = _along(list(self.positions), [uv[1] for uv in self.uvs])
        return 0.0 if d is None else abs(d[1]) / self.profile.steps_per_tile

    def box(self) -> tuple[float, float, float, float]:
        """Its extent seen from above: lowest x and z, highest x and z."""
        xs, zs = [c[0] for c in self.edge()], [c[2] for c in self.edge()]
        return min(xs), min(zs), max(xs), max(zs)


def ramps(model: cmb.Cmb, named: str, pack: TexturePack | None) -> list[Ramp]:
    """Every ramp triangle `rebuild` steps."""
    out = []
    for shape_index, slot in _stair_shapes(model, named):
        shape = model.shapes[shape_index]
        steps = profile(texture_image(model, model.textures[slot], pack))
        positions = model.vertices(shape, "position")
        uvs = model.vertices(shape, "tex0")
        indices = model.indices(shape)
        for i in range(0, len(indices) - 2, 3):
            corners = tuple(positions[j] for j in indices[i:i + 3])
            coordinates = tuple(uvs[j] for j in indices[i:i + 3])
            if _climb(list(corners), [uv[1] for uv in coordinates], steps) is not None:
                out.append(Ramp(corners, coordinates, steps))
    return out


def rebuild(model: cmb.Cmb, named: str, pack: TexturePack | None) -> tuple[bytes, list[str]]:
    """The model with every stair-textured shape stepped, and the textures it stepped. `named` is
    the game's pattern for a stair texture's name (a set's "stairs")."""
    data = model.data
    stepped = []
    for shape_index, slot in _stair_shapes(model, named):
        current = cmb.Cmb.parse(data)
        texture = current.textures[slot]
        shape = current.shapes[shape_index]
        arrays = current.arrays(shape)
        if not {"position", "normal", "tex0"} <= set(arrays):
            raise ValueError(f"{texture.name}: shape {shape_index} has arrays {arrays}")
        extras = [name for name in arrays if name not in ("position", "normal")]
        columns = [current.vertices(shape, name) for name in extras]
        # Slices hold the vertices of every later shape too; the shape's own are the shortest run.
        count = min(len(column) for column in [current.vertices(shape, "position"), *columns])
        positions = current.vertices(shape, "position")[:count]
        packed = [tuple(v for column in columns for v in column[i]) for i in range(count)]
        stairs = Stairs(positions, packed, current.vertices(shape, "tex0"), current.indices(shape),
                        profile(texture_image(current, texture, pack)))
        vertices, indices = _flat(stairs.triangles(), extras)
        data = current.with_shape(shape_index, vertices, indices)
        stepped.append(texture.name)
    return data, stepped


def _turn(polygon) -> float:
    """Twice a polygon's area seen from above, positive when it turns from +x toward +z."""
    return sum(a[0] * b[2] - b[0] * a[2] for a, b in zip(polygon, [*polygon[1:], polygon[0]]))


def _convex(polygon) -> bool:
    """Whether a polygon turns one way all round, seen from above; corners on a straight edge pass."""
    turn = _turn(polygon)
    for n, b in enumerate(polygon):
        a, c = polygon[n - 1], polygon[(n + 1) % len(polygon)]
        corner = _turn([a, b, c]) * turn
        ahead = (b[0] - a[0]) * (c[0] - b[0]) + (b[2] - a[2]) * (c[2] - b[2])
        if corner < -EPSILON or (corner <= EPSILON and ahead < 0):
            return False
    return True


def _joined(polygons: list[list[tuple[float, float, float]]], same) -> list[tuple[list, list[int]]]:
    """Convex polygons joined along shared edges wherever `same(i, j)` puts two of them in one
    surface and the two together are still convex seen from above. Each result comes with the
    indices of the polygons in it. Fewer, larger pieces mean fewer cuts later."""
    live = {i: polygon for i, polygon in enumerate(polygons)}
    members = {i: [i] for i in live}
    while True:
        edges: dict[frozenset, list[tuple[int, int]]] = {}
        for i, polygon in live.items():
            for k, a in enumerate(polygon):
                edges.setdefault(frozenset((_key(a), _key(polygon[(k + 1) % len(polygon)]))), []).append((i, k))
        touched: set[int] = set()
        for found in edges.values():
            if len(found) != 2:
                continue
            (i, k), (j, m) = found
            if i == j or i in touched or j in touched or not same(i, j):
                continue
            first, second = live[i], live[j]
            # The shared edge runs a to b in the first and b to a in the second.
            if _key(first[k]) != _key(second[(m + 1) % len(second)]):
                continue
            both = [first[(k + 1 + n) % len(first)] for n in range(len(first))]
            both += [second[(m + 2 + n) % len(second)] for n in range(len(second) - 2)]
            # Two that share more than one edge leave the others as spikes: out and straight back.
            spike = True
            while spike and len(both) > 3:
                spike = False
                for n in range(len(both)):
                    if _key(both[n - 1]) == _key(both[(n + 1) % len(both)]):
                        both = [v for at, v in enumerate(both) if at not in (n, (n + 1) % len(both))]
                        spike = True
                        break
            if len({_key(v) for v in both}) != len(both) or not _convex(both):
                continue
            live[i] = both
            members[i] += members[j]
            del live[j]
            touched |= {i, j}
        if not touched:
            return [(polygon, members[i]) for i, polygon in live.items()]


def _whole_faces(triangles: list[list[tuple[float, float, float]]]) -> list[list[tuple[int, int, int]]]:
    """Triangles on integer corners, those of one plane joined into convex polygons: a tread cut
    into many pieces is one face again. The game keeps a list entry for every triangle in every
    cell of its grid, and the list is only so long."""
    planes: dict[tuple, list[list[tuple[int, int, int]]]] = {}
    for triangle in triangles:
        corners = [tuple(round(v) for v in corner) for corner in triangle]
        normal = _cross(_sub(corners[1], corners[0]), _sub(corners[2], corners[0]))
        scale = math.gcd(*normal)
        if scale == 0:
            continue
        normal = tuple(x // scale for x in normal)
        # Seen along the axis the plane faces most, so that it keeps its shape.
        if abs(normal[1]) >= max(abs(normal[0]), abs(normal[2])):
            order = (0, 1, 2)
        elif abs(normal[0]) >= abs(normal[2]):
            order = (2, 0, 1)
        else:
            order = (0, 2, 1)
        plane = (order, normal, _dot(normal, corners[0]))
        planes.setdefault(plane, []).append([tuple(corner[axis] for axis in order) for corner in corners])
    out = []
    for (order, _, _), members in planes.items():
        back = [order.index(axis) for axis in range(3)]
        for polygon, _ in _joined(members, lambda i, j: True):
            # A corner on a straight edge is where a piece ended.
            corners = [b for a, b, c in zip(polygon[-1:] + polygon[:-1], polygon, polygon[1:] + polygon[:1])
                       if any(_cross(_sub(b, a), _sub(c, b)))]
            out.append([tuple(corner[axis] for axis in back) for corner in corners])
    return out


def _fan(polygon: list) -> list[tuple]:
    return [(polygon[0], polygon[i], polygon[i + 1]) for i in range(1, len(polygon) - 1)]


def _over_ramps(corners: list[tuple[float, float, float]], drawn: list[tuple[tuple, Ramp]]):
    """A collision triangle cut at the outlines of the drawn ramps it lies on: its parts over
    them, each with its ramp, and the parts left beside them."""
    low_x, low_z = min(c[0] for c in corners), min(c[2] for c in corners)
    high_x, high_z = max(c[0] for c in corners), max(c[2] for c in corners)
    on = []
    rest = [corners]
    for box, ramp in drawn:
        if box[0] >= high_x or box[2] <= low_x or box[1] >= high_z or box[3] <= low_z:
            continue
        beside = []
        for piece in rest:
            over, cut = ramp.cut(piece)
            if over is None or any(abs(ramp.height(x, z) - y) > ON_RAMP for x, y, z in over):
                beside.append(piece)
            else:
                on.append((over, ramp))
                beside.extend(cut)
        rest = beside
    return on, rest


def _halves(polygon: list, values: list[float]) -> tuple[list, list]:
    """A convex polygon cut where `values`, one per corner, change sign: the part below zero and
    the part above."""
    low, high = [], []
    for j, a in enumerate(polygon):
        k = (j + 1) % len(polygon)
        if values[j] <= 0:
            low.append(a)
        if values[j] >= 0:
            high.append(a)
        if values[j] * values[k] < 0:
            crossing = _lerp(a, polygon[k], values[j] / (values[j] - values[k]))
            low.append(crossing)
            high.append(crossing)
    return low, high


def _clear_of_steps(corners: list[tuple[float, float, float]], drawn: list[tuple[tuple, Ramp]]) -> list[list] | None:
    """A wall or ceiling triangle without its parts in the space the steps open under the ramps:
    between a ramp and one step below it the ROM had solid ground, and whatever ends there now
    stands in the way of feet on a tread. None when it has no part there."""
    low_x, low_z = min(c[0] for c in corners), min(c[2] for c in corners)
    high_x, high_z = max(c[0] for c in corners), max(c[2] for c in corners)
    pieces = [corners]
    cleared = False
    for box, ramp in drawn:
        if box[0] > high_x or box[2] < low_x or box[1] > high_z or box[3] < low_z:
            continue
        kept = []
        for piece in pieces:
            over, beside = ramp.cut(piece)
            if over is None:
                kept.append(piece)
                continue
            under = [y - ramp.height(x, z) for x, y, z in over]
            deep = ramp.rise() + UNDER_TREAD
            if max(under) <= -deep or min(under) >= 0:
                kept.append(piece)
                continue
            cleared = True
            kept.extend(beside)
            below, _ = _halves(over, [u + deep for u in under])
            _, above = _halves(over, under)
            kept.extend(part for part in (below, above) if len(part) >= 3)
        pieces = kept
    return pieces if cleared else None


def collision(scene: zsi.Scene, drawn: list[Ramp]) -> bytes | None:
    """The scene with the collision over stepped ramps folded into the same steps: each sloped
    floor triangle is cut at the drawn ramps' outlines, the parts over a ramp become its treads and
    risers, and the parts beside it stay; walls and ceilings lose their parts just under a ramp.
    None when no triangle lies on a ramp."""
    whole = _joined([list(ramp.positions) for ramp in drawn], lambda i, j: drawn[i].continues(drawn[j]))
    boxed = [(ramp.box(), ramp) for ramp in (replace(drawn[i[0]], outline=tuple(outline)) for outline, i in whole)]
    vertices = list(scene.vertices)
    named = {vertex: i for i, vertex in enumerate(vertices)}
    polygons = []
    groups: dict[Profile, list[tuple[int, list, Ramp]]] = {}

    def add(triangle: list[tuple[float, float, float]], source: zsi.Polygon, step: bool = False) -> None:
        corners = []
        for position in triangle:
            vertex = tuple(round(v) for v in position)
            corners.append(named.setdefault(vertex, len(vertices)))
            if corners[-1] == len(vertices):
                vertices.append(vertex)
        a, b, c = (vertices[i] for i in corners)
        if all((b[i] - a[i]) * (c[(i + 1) % 3] - a[(i + 1) % 3]) == (b[(i + 1) % 3] - a[(i + 1) % 3]) * (c[i] - a[i])
               for i in range(3)):
            return
        polygon = zsi.Polygon(source.surface, tuple(corners), (0, 0, 0), 0.0).facing(vertices, source)
        # A riser is no wall to an actor: Link's feet rise with the floor from tread to tread. As
        # walls, the risers of a steep flight stack up to a ledge he jumps. On integer corners a
        # narrow riser leans, so this is every step face the game takes for a wall.
        if step and abs(polygon.normal[1]) < math.cos(MAX_SLOPE) * zsi.NORMAL_ONE:
            polygon = polygon.passed_by_actors()
        polygons.append(polygon)

    slopes, others = [], []
    for index, polygon in enumerate(scene.polygons):
        slope = abs(polygon.normal[1]) / zsi.NORMAL_ONE
        if polygon.normal[1] > 0 and math.cos(MAX_SLOPE) <= slope <= math.cos(MIN_SLOPE):
            slopes.append(index)
        elif polygon.normal[1] > 0 and slope > math.cos(MAX_SLOPE):
            polygons.append(polygon)
        else:
            others.append(index)

    def surface(index: int) -> tuple:
        polygon = scene.polygons[index]
        return (polygon.surface, tuple(c & ~zsi.VERTEX_INDEX for c in polygon.corners), polygon.normal,
                polygon.distance, polygon.word)

    corners = [[tuple(float(v) for v in scene.vertices[i]) for i in scene.polygons[index].vertices()]
               for index in slopes]
    for outline, members in _joined(corners, lambda i, j: surface(slopes[i]) == surface(slopes[j])):
        index = slopes[members[0]]
        polygon = scene.polygons[index]
        on, rest = _over_ramps(outline, boxed)
        if not on:
            polygons.extend(scene.polygons[slopes[i]] for i in members)
            continue
        for piece in rest:
            for triangle in _fan(piece):
                add(triangle, polygon)
        for over, ramp in on:
            groups.setdefault(ramp.profile, []).append((index, over, ramp))
    if not groups:
        return None
    stepped = [(ramp.box(), ramp) for ramp in {id(ramp): ramp for group in groups.values() for _, _, ramp in group}.values()]
    for index in others:
        polygon = scene.polygons[index]
        pieces = _clear_of_steps([tuple(float(v) for v in scene.vertices[i]) for i in polygon.vertices()], stepped)
        if pieces is None:
            polygons.append(polygon)
            continue
        for piece in pieces:
            for triangle in _fan(piece):
                add(triangle, polygon)
    for steps, group in groups.items():
        positions, sources, uvs = [], [], []
        for index, over, ramp in group:
            for triangle in _fan(over):
                for x, y, z in triangle:
                    positions.append((x, y, z))
                    sources.append((float(index),))
                    uvs.append(ramp.uv(x, z))
        faces: dict[int, list[list[tuple[float, float, float]]]] = {}
        for triangle in Stairs(positions, sources, uvs, list(range(len(positions))), steps).floor():
            faces.setdefault(round(triangle[0][1][0]), []).append([position for position, _ in triangle])
        for index, triangles in faces.items():
            for polygon in _whole_faces(triangles):
                for triangle in _fan(polygon):
                    add(triangle, scene.polygons[index], step=True)
    return scene.with_collision(vertices, polygons)


def room(data: bytes, named: str, pack: TexturePack | None) -> tuple[bytes, list[str]]:
    """A room file with its stair ramps stepped; unchanged when it has none."""
    room_ = zsi.Room.parse(data)
    stepped: list[str] = []
    for model in reversed(room_.models()):
        cmb_bytes, names = rebuild(cmb.Cmb.parse(data[model:]), named, pack)
        if names:
            data = zsi.Room.parse(data).with_model(model, cmb_bytes)
            stepped.extend(names)
    return data, stepped


def room_ramps(data: bytes, named: str, pack: TexturePack | None) -> list[Ramp]:
    """The ramp triangles `room` steps, as the room file has them."""
    return [ramp for model in zsi.Room.parse(data).models()
            for ramp in ramps(cmb.Cmb.parse(data[model:]), named, pack)]


def scene_of(path: str) -> str | None:
    """The scene file a room file belongs to; None when `path` is not a room."""
    found = ROOM_FILE.match(path)
    return f"{found.group(1)}/{found.group(2)}_info.zsi" if found else None
