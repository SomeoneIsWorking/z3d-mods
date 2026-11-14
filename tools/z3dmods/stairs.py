"""Stepped geometry for OoT3D's stair ramps.

The game draws stairs as smooth ramps with steps painted on (textures named *kaidan*, "stairs").
The ramp is the stairs' pitch line, the line through the step noses, as on real stairs: each ramp
is cut where its texture paints a step edge and folded into vertical risers and flat treads below
it, with each nose on the ramp where the paint has it. The curbs the texture paints along its sides
stay flat on the ramp, and the steps sit between them; side faces close every step edge up to the
ramp. Nothing rises above the ramp, so the steps stay inside the walls that meet it. The scene's
collision triangles that lie on a stepped ramp are folded the same way, so Link walks the treads.
"""

from __future__ import annotations

import cmath
import math
import re
from dataclasses import dataclass

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
# A collision triangle lies on a drawn ramp when it is this close to it: above or below, and
# sideways past the ramp's edge (N64 units; collision vertices are integers).
ON_RAMP = 4.0
PAST_EDGE = 8.0


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
                vertex.position[1] + d[1] * (up - t) * scale,
                vertex.position[2] + d[2] * (across - t) * scale)

    def triangles(self, sides: bool = True) -> list[tuple[tuple[tuple[float, ...], tuple[float, ...]], ...]]:
        """Output triangles as three (position, extra) corners, flat shaded by the caller. `sides`
        adds the faces that close the steps' ends."""
        out = []
        stepped = []
        for tri in self._triangles:
            for column in _split(tri, self._u, self._u_levels):
                for piece in _split(column, self._t, self._t_levels):
                    folds = self._stepped(piece)
                    corners = [(self._place(v) if folds else v.position, v.extra) for v in piece]
                    out.extend((corners[0], corners[i], corners[i + 1]) for i in range(1, len(corners) - 1))
                    if folds:
                        stepped.append(piece)
        if not sides:
            return out
        # The steps' sides, up to the ramp wherever no other step continues them: the curbs' inner
        # faces and the ramp's open edges, seen from either side.
        edges: dict[frozenset, list[tuple[Vertex, Vertex]]] = {}
        for piece in stepped:
            for a, b in zip(piece, piece[1:] + piece[:1]):
                edges.setdefault(frozenset((_key(a.position), _key(b.position))), []).append((a, b))
        for found in edges.values():
            if len(found) != 1:
                continue
            a, b = found[0]
            edge_a, edge_b = (a.position, a.extra), (b.position, b.extra)
            step_a, step_b = (self._place(a), a.extra), (self._place(b), b.extra)
            for quad in ((edge_a, edge_b, step_b, step_a), (edge_a, step_a, step_b, edge_b)):
                out.append((quad[0], quad[1], quad[2]))
                out.append((quad[0], quad[2], quad[3]))
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
    image = pica_texture.decode(model.texture_data(texture), texture.width, texture.height,
                                texture.format)
    found = pack.replacement(image, texture.format) if pack else None
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

    def reach(self, x: float, z: float) -> float:
        """How far (x, z) lies outside the triangle seen from above; 0 inside."""
        worst = 0.0
        p = self.positions
        turn = 0.0
        for i in range(3):
            (ax, _, az), (bx, _, bz) = p[i], p[(i + 1) % 3]
            turn += ax * bz - bx * az
        for i in range(3):
            (ax, _, az), (bx, _, bz) = p[i], p[(i + 1) % 3]
            length = math.hypot(bx - ax, bz - az)
            if length < EPSILON:
                continue
            side = ((bx - ax) * (z - az) - (bz - az) * (x - ax)) / length
            worst = max(worst, -side if turn > 0 else side)
        return worst


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
            if _along(list(corners), [uv[1] for uv in coordinates]) is not None:
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


def _on_ramp(corners: list[tuple[int, int, int]], drawn: list[Ramp]) -> Ramp | None:
    """The drawn ramp triangle a collision triangle lies on with all three corners, if any."""
    centre = [sum(c[axis] for c in corners) / 3 for axis in range(3)]
    under = None
    for ramp in drawn:
        if ramp.reach(centre[0], centre[2]) > 0:
            continue
        height = ramp.height(centre[0], centre[2])
        if height is not None and abs(height - centre[1]) <= ON_RAMP:
            under = ramp
            break
    if under is None:
        return None
    for x, y, z in corners:
        if abs(under.height(x, z) - y) > ON_RAMP:
            return None
        if not any(r.profile == under.profile and r.reach(x, z) <= PAST_EDGE
                   and abs(r.height(x, z) - y) <= ON_RAMP for r in drawn if r.height(x, z) is not None):
            return None
    return under


def collision(scene: zsi.Scene, drawn: list[Ramp]) -> bytes | None:
    """The scene with the collision triangles on stepped ramps folded into the same steps; None
    when no triangle lies on one."""
    groups: dict[Profile, list[tuple[int, Ramp]]] = {}
    for index, polygon in enumerate(scene.polygons):
        corners = [scene.vertices[i] for i in polygon.vertices()]
        slope = abs(polygon.normal[1]) / zsi.NORMAL_ONE
        if polygon.normal[1] <= 0 or not math.cos(MAX_SLOPE) <= slope <= math.cos(MIN_SLOPE):
            continue
        ramp = _on_ramp(corners, drawn)
        if ramp is not None:
            groups.setdefault(ramp.profile, []).append((index, ramp))
    if not groups:
        return None
    vertices = list(scene.vertices)
    named = {vertex: i for i, vertex in enumerate(vertices)}
    folded = {index for group in groups.values() for index, _ in group}
    polygons = [polygon for index, polygon in enumerate(scene.polygons) if index not in folded]
    for steps, group in groups.items():
        positions, sources, uvs = [], [], []
        for index, ramp in group:
            for corner in scene.polygons[index].vertices():
                x, y, z = scene.vertices[corner]
                positions.append((float(x), float(y), float(z)))
                sources.append((float(index),))
                uvs.append(ramp.uv(x, z))
        # No side faces: they are lower than the height the game checks walls at.
        for triangle in Stairs(positions, sources, uvs, list(range(len(positions))), steps).triangles(sides=False):
            source = scene.polygons[round(triangle[0][1][0])]
            corners = []
            for position, _ in triangle:
                vertex = tuple(round(v) for v in position)
                corners.append(named.setdefault(vertex, len(vertices)))
                if corners[-1] == len(vertices):
                    vertices.append(vertex)
            if len(set(corners)) < 3:
                continue
            a, b, c = (vertices[i] for i in corners)
            if all((b[i] - a[i]) * (c[(i + 1) % 3] - a[(i + 1) % 3]) == (b[(i + 1) % 3] - a[(i + 1) % 3]) * (c[i] - a[i])
                   for i in range(3)):
                continue
            polygons.append(zsi.Polygon(source.surface, tuple(corners), (0, 0, 0), 0.0).facing(vertices, source))
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
