"""Stone steps: each stair flight rebuilt as square steps of a chosen rise, wearing our own stone.

A flight is the ramp triangles of one shape that touch and lie in one plane. Seen along its uphill
direction it is cut into N = round(height / rise) equal steps; each tread sits a full step above the
ramp at its front, so its back meets the ramp and the last tread is level with the landing. Risers
stand at the back of each tread, the first step has a front, and open flight edges are closed by
side caps down to the ramp.

Across the flight, the columns the game's texture paints as curbs stay curbs: walls whose tops
run on the step noses, drawn with the game's own texture at the ramp's own coordinates. The steps
wear our stone (sets/stairs/stone.svg), drawn into the same texture's step columns, so one texture
and one material carry both. The collision is the treads, risers, fronts and curb tops.
"""

from __future__ import annotations

import io
import itertools
import math
import struct
from dataclasses import dataclass, field, replace

from PIL import Image

from z3dmods import cmb, config, etc1, glyphs, pica_texture
from z3dmods.stairs import (
    KEY,
    Profile,
    Ramp,
    Shape,
    _along,
    _cross,
    _dot,
    _fan,
    _key,
    _sub,
    _unit,
    shape_ramps,
)
from z3dmods.texture_pack import TexturePack

STONE_SVG = config.REPO / "sets" / "stairs" / "stone.svg"
# World units one stone tile spans across the flight.
TILE = 44.0
# The stone's nose line in v: the tread runs from it down to 0 at its back, the riser from it up
# to 1 at its foot (stone.svg's row 97 of 256).
NOSE = 0.62
# Vertex colour shades by face: lit treads, risers less, side faces least.
TREAD, RISER, SIDE = 1.0, 0.72, 0.55
# Ramp triangles whose normals are this close are one plane.
COPLANAR = 0.98
# Samples across a flight where its painted curbs are read; a span narrower than MIN_SPAN joins
# its neighbours.
SAMPLES = 256
MIN_SPAN = 1.0
# Stone texels kept clear of the curb columns, so filtering and mip levels do not mix them.
INSET = 2
EPSILON = 1e-6


@dataclass(frozen=True)
class Span:
    low: float
    high: float
    curb: bool


@dataclass
class Face:
    """One face of a flight: corners, which way it faces, its shade, and its texture coordinates:
    stone (s across in tiles, v in the step tile), or none for a curb, which takes the ramp's."""

    corners: list[tuple[float, float, float]]
    facing: tuple[float, float, float]
    shade: float
    stone: list[tuple[float, float]] | None
    solid: bool


@dataclass
class Flight:
    ramps: list[Ramp]
    # Indices of its ramps in the list it was made from.
    members: list[int]
    uphill: tuple[float, float]
    low_a: float
    high_a: float
    low_c: float
    high_c: float
    low_y: float
    high_y: float
    steps: int
    spans: list[Span] = field(default_factory=list)

    @property
    def profile(self) -> Profile:
        return self.ramps[0].profile

    def world(self, a: float, y: float, c: float) -> tuple[float, float, float]:
        hx, hz = self.uphill
        return (hx * a + hz * c, y, hz * a - hx * c)

    def plan(self, x: float, z: float) -> tuple[float, float]:
        hx, hz = self.uphill
        return (hx * x + hz * z, hz * x - hx * z)

    def ramp_y(self, a: float) -> float:
        return self.low_y + (a - self.low_a) / (self.high_a - self.low_a) * (self.high_y - self.low_y)

    def under(self, x: float, z: float) -> Ramp | None:
        """The ramp triangle under (x, z), or None."""
        for ramp in self.ramps:
            w = ramp._weights(x, z)
            if w is not None and min(w) >= -1e-4:
                return ramp
        return None

    def nearest(self, x: float, z: float) -> Ramp:
        """The ramp triangle under (x, z), else the one whose centre is nearest."""
        return self.under(x, z) or min(self.ramps, key=lambda r: math.hypot(
            sum(p[0] for p in r.positions) / 3 - x, sum(p[2] for p in r.positions) / 3 - z))

    def _read_spans(self) -> None:
        """Split the width into stepped spans and curbs where the texture paints them, read from
        the triangle under each sample along the middle: a flight is often strips with their own u."""
        p = self.profile
        middle = (self.low_a + self.high_a) / 2
        width = (self.high_c - self.low_c) / SAMPLES
        spans: list[Span] = []
        for i in range(SAMPLES):
            curb = False
            if p.first > 0 or p.last < 1:
                x, _, z = self.world(middle, 0.0, self.low_c + (i + 0.5) * width)
                ramp = self.under(x, z)
                if ramp is not None:
                    u = ramp.uv(x, z)[0] % 1
                    curb = u < p.first or u > p.last
            high = self.high_c if i + 1 == SAMPLES else self.low_c + (i + 1) * width
            if spans and spans[-1].curb == curb:
                spans[-1] = replace(spans[-1], high=high)
            else:
                spans.append(Span(self.low_c + i * width, high, curb))
        while len(spans) > 1:
            thin = next((i for i, s in enumerate(spans) if s.high - s.low < MIN_SPAN), None)
            if thin is None:
                break
            if thin > 0:
                spans[thin - 1] = replace(spans[thin - 1], high=spans[thin].high)
            else:
                spans[1] = replace(spans[1], low=spans[0].low)
            del spans[thin]
            merged = [spans[0]]
            for s in spans[1:]:
                if s.curb == merged[-1].curb:
                    merged[-1] = replace(merged[-1], high=s.high)
                else:
                    merged.append(s)
            spans = merged
        self.spans = spans

    def faces(self) -> list[Face]:
        """Every face of the flight, the stepped spans' in stone and the curbs' in the ramp's paint."""
        da = (self.high_a - self.low_a) / self.steps
        dy = (self.high_y - self.low_y) / self.steps
        hx, hz = self.uphill
        up, down = (0.0, 1.0, 0.0), (-hx, 0.0, -hz)
        # Toward lower and higher c.
        lower, higher = (-hz, 0.0, hx), (hz, 0.0, -hx)
        out: list[Face] = []

        def add(points, facing, shade, stone, solid):
            out.append(Face([self.world(*p) for p in points], facing, shade, stone, solid))

        for n, span in enumerate(self.spans):
            c0, c1 = span.low, span.high
            s0, s1 = c0 / TILE, c1 / TILE
            first, last = n == 0, n + 1 == len(self.spans)
            curb_lower = n > 0 and not self.spans[n - 1].curb
            curb_higher = not last and not self.spans[n + 1].curb
            for k in range(self.steps):
                a0, a1 = self.low_a + k * da, self.low_a + (k + 1) * da
                ramp0, ramp1 = self.ramp_y(a0), self.ramp_y(a1)
                y = ramp0 + dy
                if not span.curb:
                    add([(a0, y, c0), (a1, y, c0), (a1, y, c1), (a0, y, c1)], up, TREAD,
                        [(s0, NOSE), (s0, 0.0), (s1, 0.0), (s1, NOSE)], True)
                    # The last tread meets the landing: no riser behind it.
                    if k + 1 < self.steps:
                        add([(a1, y, c0), (a1, y + dy, c0), (a1, y + dy, c1), (a1, y, c1)], down, RISER,
                            [(s0, 1.0), (s0, NOSE), (s1, NOSE), (s1, 1.0)], True)
                    sa0, sa1 = a0 / TILE, a1 / TILE
                    for at, facing, edge in ((c0, lower, first), (c1, higher, last)):
                        if edge:
                            add([(a0, ramp0, at), (a0, y, at), (a1, y, at)], facing, SIDE,
                                [(sa0, 1.0), (sa0, NOSE), (sa1, NOSE)], False)
                    continue
                top0, top1 = min(ramp0 + dy, self.high_y), min(ramp1 + dy, self.high_y)
                add([(a0, top0, c0), (a1, top1, c0), (a1, top1, c1), (a0, top0, c1)], up, TREAD, None, True)
                for at, facing, edge, beside in ((c0, lower, first, curb_lower), (c1, higher, last, curb_higher)):
                    if edge:
                        add([(a0, ramp0, at), (a1, ramp1, at), (a1, top1, at), (a0, top0, at)], facing, RISER,
                            None, False)
                    elif beside and top1 > top0 + KEY:
                        # Above the tread beside it, which is level at top0.
                        add([(a0, top0, at), (a1, top0, at), (a1, top1, at)], facing, RISER, None, False)
            front = [(self.low_a, self.low_y, c0), (self.low_a, self.low_y + dy, c0),
                     (self.low_a, self.low_y + dy, c1), (self.low_a, self.low_y, c1)]
            add(front, down, RISER, None if span.curb else [(s0, 1.0), (s0, NOSE), (s1, NOSE), (s1, 1.0)], True)
        return out


def flights(ramps: list[Ramp], rise: float) -> list[Flight]:
    """The ramp triangles grouped into flights: one shape's triangles that share a corner and lie
    in one plane. Each is cut into steps `rise` units high."""
    parent = list(range(len(ramps)))

    def root(i: int) -> int:
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i

    normals = [_unit(_cross(_sub(r.positions[1], r.positions[0]), _sub(r.positions[2], r.positions[0]))) for r in ramps]
    normals = [n if n[1] >= 0 else tuple(-x for x in n) for n in normals]
    corners: dict[tuple, list[int]] = {}
    for i, ramp in enumerate(ramps):
        for p in ramp.positions:
            corners.setdefault((ramp.shape, _key(p)), []).append(i)
    for found in corners.values():
        for i in found[1:]:
            if _dot(normals[i], normals[found[0]]) > COPLANAR:
                parent[root(i)] = root(found[0])
    groups: dict[int, list[int]] = {}
    for i in range(len(ramps)):
        groups.setdefault(root(i), []).append(i)
    out = []
    for members in groups.values():
        n = [sum(normals[i][axis] for i in members) for axis in range(3)]
        across = math.hypot(n[0], n[2])
        if across < EPSILON:
            continue
        uphill = (-n[0] / across, -n[2] / across)
        flight = Flight([ramps[i] for i in members], members, uphill, 0, 0, 0, 0, 0, 0, 1)
        points = [p for i in members for p in ramps[i].positions]
        plan = [flight.plan(p[0], p[2]) for p in points]
        flight.low_a, flight.high_a = min(a for a, _ in plan), max(a for a, _ in plan)
        flight.low_c, flight.high_c = min(c for _, c in plan), max(c for _, c in plan)
        flight.low_y, flight.high_y = min(p[1] for p in points), max(p[1] for p in points)
        if min(flight.high_a - flight.low_a, flight.high_c - flight.low_c, flight.high_y - flight.low_y) < 1:
            continue
        flight.steps = max(1, round((flight.high_y - flight.low_y) / rise))
        flight._read_spans()
        out.append(flight)
    return out


def _cut(corners: list[tuple], at: float) -> tuple[list[tuple], list[tuple]]:
    """A convex polygon of (position, s, v) corners cut where s crosses `at`: below and above."""
    low, high = [], []
    for i, a in enumerate(corners):
        b = corners[(i + 1) % len(corners)]
        if a[1] <= at + EPSILON:
            low.append(a)
        if a[1] >= at - EPSILON:
            high.append(a)
        if (a[1] - at) * (b[1] - at) < 0:
            f = (at - a[1]) / (b[1] - a[1])
            cut = (tuple(x + (y - x) * f for x, y in zip(a[0], b[0])), at, a[2] + (b[2] - a[2]) * f)
            low.append(cut)
            high.append(cut)
    return low, high


def _tiles(face: Face) -> list[list[tuple]]:
    """A stone face cut at every whole tile across: (position, s within its tile, v) corners."""
    corners = [(p, s, v) for p, (s, v) in zip(face.corners, face.stone)]
    low, high = min(c[1] for c in corners), max(c[1] for c in corners)
    out = []
    rest = corners
    for tile in range(math.floor(low + EPSILON) + 1, math.ceil(high - EPSILON)):
        below, rest = _cut(rest, tile)
        if len(below) >= 3:
            out.append(below)
    if len(rest) >= 3:
        out.append(rest)
    shifted = []
    for piece in out:
        tile = math.floor(min(c[1] for c in piece) + EPSILON)
        shifted.append([(p, s - tile, v) for p, s, v in piece])
    return shifted


def _wound(triangle: list, facing: tuple[float, float, float], position=lambda c: c[0]) -> list:
    """The triangle wound counter-clockwise seen from `facing`."""
    a, b, c = (position(corner) for corner in triangle)
    if _dot(_cross(_sub(b, a), _sub(c, a)), facing) < 0:
        return [triangle[0], triangle[2], triangle[1]]
    return triangle


@dataclass(frozen=True)
class Rect:
    """Where the stone sits in a stair texture, in texture coordinates within one tile."""

    low_u: float
    high_u: float
    low_v: float
    high_v: float

    def at(self, s: float, v: float) -> tuple[float, float]:
        return self.low_u + s * (self.high_u - self.low_u), self.low_v + v * (self.high_v - self.low_v)


def _in_tile(values: list[float]) -> tuple[float, float]:
    """The span of `values` moved into the tile it starts in; a span over a whole tile is the tile."""
    tile = math.floor(min(values) + EPSILON)
    low, high = min(values) - tile, max(values) - tile
    return (0.0, 1.0) if high - low >= 1 - EPSILON else (low, high)


def _overlaps(span: tuple[float, float], low: float, high: float, slack: float) -> bool:
    return any(span[0] + shift < high - slack and span[1] + shift > low + slack for shift in (-1, 0, 1))


def _widest(spans: list[tuple[float, float]], slack: float) -> tuple[float, float]:
    merged: list[list[float]] = []
    for low, high in sorted(spans):
        if merged and low <= merged[-1][1] + slack:
            merged[-1][1] = max(merged[-1][1], high)
        else:
            merged.append([low, high])
    return tuple(max(merged, key=lambda m: m[1] - m[0]))


def _inset(low: float, high: float, texels: float) -> tuple[float, float]:
    return (0.0, 1.0) if low <= 0 and high >= 1 else (low + texels, high - texels)


def stone_rect(source: cmb.Cmb, slot: int, steps: Profile, rise: float) -> Rect:
    """The stone's place in texture `slot`: the widest run of step columns the flights paint, and
    across it the widest band of rows no other triangle of those shapes samples, so they keep the
    game's paint where they can."""
    texture = source.textures[slot]
    climbs, painted = [], []
    for index in sorted({mesh.shape for mesh in source.meshes if source.materials[mesh.material][0] == slot}):
        stepped = {tuple(tri) for tri, _ in shape_ramps(source, index, steps, rise)}
        shape = source.shapes[index]
        uvs = source.vertices(shape, "tex0")
        indices = source.indices(shape)
        for i in range(0, len(indices) - 2, 3):
            tri = indices[i:i + 3]
            box = _in_tile([uvs[j][0] for j in tri]), _in_tile([uvs[j][1] for j in tri])
            (climbs if tuple(tri) in stepped else painted).append(box)
    if not climbs:
        raise ValueError(f"{texture.name}: no flight paints it")
    slack_u, slack_v = 1 / texture.width, 1 / texture.height
    low_u, high_u = _widest([u for u, _ in climbs], slack_u)
    low_u, high_u = _inset(max(low_u, steps.first), min(high_u, steps.last), INSET / texture.width)
    # A triangle over a whole tile of rows meets the stone in any band.
    taken = [v for u, v in painted if v != (0.0, 1.0) and _overlaps(u, low_u, high_u, slack_u)]
    edges = sorted({0.0, 1.0, *(x for v in taken for x in v)})
    free = [(a, b) for a, b in itertools.pairwise(edges)
            if not any(_overlaps(v, a, b, slack_v) for v in taken)]
    if not free:
        raise ValueError(f"{texture.name}: no rows free of the game's other paint in its step columns")
    low_v, high_v = _widest(free, slack_v)
    low_v, high_v = _inset(low_v, high_v, INSET / texture.height)
    return Rect(low_u, high_u, low_v, high_v)


def _curb_uv(ramp: Ramp, point: tuple[float, float, float]) -> tuple[float, float]:
    """The ramp's own texture coordinates at a curb corner; one above the ramp continues its v up."""
    x, y, z = point
    u, v = ramp.uv(x, z)
    d = _along(list(ramp.positions), [uv[1] for uv in ramp.uvs])
    if d is not None and abs(d[1]) > EPSILON:
        v += (y - ramp.height(x, z)) / d[1]
    return u, v


class Stone:
    """Stone steps `rise` units high. `textures` makes the stone into each stair texture, shared by
    every size."""

    clears_under = False

    def __init__(self, rise: float, textures: StoneTextures) -> None:
        self.rise = rise
        self._textures = textures

    def triangles(self, shape: Shape) -> list | None:
        """The shape's flights in stone; None for a shape with none, which keeps its paint."""
        found = shape_ramps(shape.model, shape.index, shape.steps, self.rise)
        if not found:
            return None
        tex0, color = shape.slot("tex0"), shape.slot("color")
        rect = stone_rect(shape.source, shape.texture, shape.steps, self.rise)
        stepped = {tuple(tri) for tri, _ in found}
        out = []
        tris = [shape.indices[i:i + 3] for i in range(0, len(shape.indices) - 2, 3)]
        for tri in tris:
            if tuple(tri) not in stepped:
                out.append(tuple((shape.positions[j], shape.packed[j]) for j in tri))
        # The curbs wear the game's paint at the ramp's own coordinates, not the resized ones.
        ramps = [replace(ramp, uvs=tuple(shape.packed[j][tex0:tex0 + 2] for j in tri)) for tri, ramp in found]
        for flight in flights(ramps, self.rise):
            members = [found[i][0] for i in flight.members]
            template = shape.packed[members[0][0]]
            base = None
            if color is not None:
                corners = [shape.packed[j][color:color + 4] for tri in members for j in tri]
                base = [sum(c[i] for c in corners) / len(corners) for i in range(4)]

            def extra(uv: tuple[float, float], shade: float, template=template, base=base) -> tuple[float, ...]:
                values = list(template)
                values[tex0:tex0 + 2] = uv
                if base is not None:
                    values[color:color + 4] = [base[0] * shade, base[1] * shade, base[2] * shade, base[3]]
                return tuple(values)

            for face in flight.faces():
                if face.stone is None:
                    centre = [sum(p[axis] for p in face.corners) / len(face.corners) for axis in range(3)]
                    ramp = flight.nearest(centre[0], centre[2])
                    polygon = [(p, extra(_curb_uv(ramp, p), face.shade)) for p in face.corners]
                    out.extend(_wound(list(t), face.facing) for t in _fan(polygon))
                    continue
                for piece in _tiles(face):
                    polygon = [(p, extra(rect.at(s, v), face.shade)) for p, s, v in piece]
                    out.extend(_wound(list(t), face.facing) for t in _fan(polygon))
        return [tuple(t) for t in out]

    def retexture(self, data: bytes, source: cmb.Cmb, slot: int, steps: Profile, image: Image.Image) -> bytes:
        return self._textures.retexture(data, slot, stone_rect(source, slot, steps, self.rise), image)

    def floor(self, pieces: list[tuple[int, list, Ramp]], drawn: list[Ramp]) -> list[tuple[int, list]]:
        """The flights' solid faces over each collision piece, cut at the piece's outline."""
        made = flights(drawn, self.rise)
        by_ramp = {(ramp.shape, ramp.positions): flight for flight in made for ramp in flight.ramps}
        solid: dict[int, list[list[tuple[float, float, float]]]] = {}
        out = []
        seen = set()
        for index, over, ramp in pieces:
            flight = by_ramp.get((ramp.shape, ramp.positions))
            if flight is None:
                continue
            if id(flight) not in solid:
                solid[id(flight)] = [f.corners for f in flight.faces() if f.solid]
            for face in solid[id(flight)]:
                part = _clip(face, over)
                if len(part) < 3:
                    continue
                for triangle in _fan(part):
                    key = frozenset(tuple(round(v) for v in p) for p in triangle)
                    if len(key) < 3 or key in seen:
                        continue
                    seen.add(key)
                    out.append((index, list(triangle)))
        return out


def _clip(polygon: list[tuple[float, float, float]], outline: list[tuple[float, float, float]]) -> list:
    """A planar polygon cut to a convex outline seen from above."""
    turn = sum(a[0] * b[2] - b[0] * a[2] for a, b in zip(outline, outline[1:] + outline[:1]))
    sign = 1 if turn > 0 else -1
    out = polygon
    for i, a in enumerate(outline):
        b = outline[(i + 1) % len(outline)]
        if not out:
            break
        inward = [sign * ((b[0] - a[0]) * (p[2] - a[2]) - (b[2] - a[2]) * (p[0] - a[0])) for p in out]
        kept = []
        for j, p in enumerate(out):
            k = (j + 1) % len(out)
            if inward[j] >= -EPSILON:
                kept.append(p)
            if (inward[j] > EPSILON and inward[k] < -EPSILON) or (inward[j] < -EPSILON and inward[k] > EPSILON):
                f = inward[j] / (inward[j] - inward[k])
                kept.append(tuple(x + (y - x) * f for x, y in zip(p, out[k])))
        out = kept
    return out


class StoneTextures:
    """Each stair texture with our stone drawn into its step columns, the curb columns kept: the
    game's copy in the texture's own format and size, and a large copy for the texture pack, named
    by Azahar's hash of the game's copy."""

    def __init__(self, pack: TexturePack | None) -> None:
        self._pack = pack
        self._made: dict[tuple, bytes] = {}
        # Pack file name to PNG bytes.
        self.pack_images: dict[str, bytes] = {}
        self._stone: Image.Image | None = None

    def _drawn(self) -> Image.Image:
        if self._stone is None:
            self._stone = glyphs.image(STONE_SVG).convert("RGB")
        return self._stone

    def _composite(self, image: Image.Image, rect: Rect, width: int, height: int) -> Image.Image:
        out = image.convert("RGBA").resize((width, height), Image.LANCZOS)
        left, right = round(rect.low_u * width), round(rect.high_u * width)
        # Rows run down as v falls.
        top, bottom = round((1 - rect.high_v) * height), round((1 - rect.low_v) * height)
        if right > left and bottom > top:
            stone = self._drawn().resize((right - left, bottom - top), Image.LANCZOS).convert("RGBA")
            out.paste(stone, (left, top))
        return out

    def retexture(self, data: bytes, slot: int, rect: Rect, image: Image.Image) -> bytes:
        """The model with texture `slot`'s pixels made stone over `rect`. `image` is the texture as
        the player saw it before (the pack's copy when there is one)."""
        model = cmb.Cmb.parse(data)
        texture = model.textures[slot]
        if texture.format not in (etc1.ETC1, etc1.ETC1A4):
            raise ValueError(f"{texture.name}: format 0x{texture.format:X}, stone is made in ETC1 only")
        alpha = texture.format == etc1.ETC1A4
        key = (texture.name, texture.width, texture.height, texture.format, texture.size, rect, image.size)
        if key not in self._made:
            levels = []
            width, height = texture.width, texture.height
            while sum(len(level) for level in levels) < texture.size:
                small = self._composite(image, rect, width, height)
                levels.append(etc1.encode(list(small.getdata()), width, height, alpha))
                width, height = width // 2, height // 2
            pixels = b"".join(levels)
            if len(pixels) != texture.size:
                raise ValueError(f"{texture.name}: {len(pixels)} bytes of levels for a texture of {texture.size}")
            self._made[key] = pixels
            if self._pack is not None:
                large = self._composite(image, rect, max(image.width, texture.width), max(image.height, texture.height))
                first = pixels[:pica_texture.level_size(texture.width, texture.height, texture.format)]
                decoded = pica_texture.decode(first, texture.width, texture.height, texture.format)
                png = io.BytesIO()
                large.save(png, "PNG")
                self.pack_images[self._pack.name(decoded, first, texture.format)] = png.getvalue()
        start = struct.unpack_from("<I", data, model.layout.texture_data)[0] + texture.offset
        return data[:start] + self._made[key] + data[start + texture.size:]
