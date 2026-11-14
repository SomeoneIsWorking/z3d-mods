"""OoT3D and MM3D room and scene files (.zsi): header commands, a room's CMB models, a scene's collision.

Header offsets count from 0x10, the end of the file header. Command layout after noclip.website's
src/OcarinaOfTime3D/zsi.ts (MIT, Jasper St. Pierre and contributors).
"""

from __future__ import annotations

import struct
from dataclasses import dataclass, replace

# OoT3D, MM3D.
MAGICS = (b"ZSI\x01", b"ZSI\x09")
BASE = 0x10
END = 0x14
MESH = 0x0A
MULTI_SETUP = 0x18
# Commands whose word is an offset into the file.
POINTERS = {0x01, MESH, 0x0B, MULTI_SETUP}
MESH_ENTRY_MODELS = (0x08, 0x0C)
CMB_MAGIC = b"cmb "
CMB_SIZE = 0x04


@dataclass(frozen=True)
class Room:
    data: bytes
    # File offsets of every header command, main setup and alternates.
    commands: list[int]
    # File offsets of every mesh entry's model pointer that is set.
    model_pointers: list[int]

    @classmethod
    def parse(cls, data: bytes) -> Room:
        if data[0:4] not in MAGICS:
            raise ValueError("not a room file")
        commands: list[int] = []
        pointers: list[int] = []
        lists = [BASE]
        seen: set[int] = set()
        while lists:
            offset = lists.pop()
            if offset in seen:
                continue
            seen.add(offset)
            while True:
                code, count, word = struct.unpack_from("<BBxxI", data, offset)
                commands.append(offset)
                if code == END:
                    break
                if code == MULTI_SETUP:
                    for i in range(count):
                        setup = struct.unpack_from("<I", data, BASE + word + i * 4)[0]
                        if setup:
                            lists.append(BASE + setup)
                if code == MESH:
                    pointers.extend(_model_pointers(data, BASE + word))
                offset += 8
        return cls(data, commands, sorted(set(pointers)))

    def models(self) -> list[int]:
        """File offsets of the room's CMB models."""
        offsets = (BASE + struct.unpack_from("<I", self.data, p)[0] for p in self.model_pointers)
        return [o for o in offsets if self.data[o:o + 4] == CMB_MAGIC]

    def with_model(self, model: int, cmb: bytes) -> bytes:
        """The room with the CMB at a file offset replaced; everything after it moves."""
        old_size = struct.unpack_from("<I", self.data, model + CMB_SIZE)[0]
        end = model - BASE + old_size
        grow = len(cmb) - old_size
        out = bytearray(self.data[:model] + cmb + self.data[model + old_size:])

        locations: set[int] = set()
        for command in self.commands:
            code, count, word = struct.unpack_from("<BBxxI", self.data, command)
            if code in POINTERS:
                locations.add(command + 4)
            if code == MULTI_SETUP:
                locations.update(BASE + word + i * 4 for i in range(count))
            if code == MESH:
                locations.add(BASE + word + 4)
        locations.update(self.model_pointers)
        for at in locations:
            value = struct.unpack_from("<I", self.data, at)[0]
            if value >= end:
                struct.pack_into("<I", out, at + (grow if at >= model + old_size else 0), value + grow)
        return bytes(out)


def _model_pointers(data: bytes, header: int) -> list[int]:
    count = data[header + 1]
    entries = BASE + struct.unpack_from("<I", data, header + 4)[0]
    if count == 0:
        return []
    if count != 1:
        raise ValueError(f"mesh header with {count} entries")
    return [entries + field for field in MESH_ENTRY_MODELS
            if struct.unpack_from("<I", data, entries + field)[0]]


COLLISION = 0x03
# Collision header fields (decomp/oot3d docs/notes/scene_collision.md).
# The vertex count, then the triangle count, by the file's magic: MM3D's header has two more
# bytes before its bounds.
COLLISION_COUNTS = {MAGICS[0]: 0x0C, MAGICS[1]: 0x0E}
COLLISION_VERTICES = 0x18
COLLISION_POLYGONS = 0x1C
VERTEX_SIZE = 6
POLYGON_SIZE = 20
# A triangle is its type, three corners, the normal and the plane distance, with one more word:
# before the normal in OoT3D, after it (always zero) in MM3D.
POLYGON_WORD_FIRST = {MAGICS[0]: True, MAGICS[1]: False}
VERTEX_INDEX = 0x1FFF
# In the first corner's flags: actors pass through the triangle; the camera and projectiles do not.
IGNORE_ENTITY = 0x4000
MAX_VERTICES = VERTEX_INDEX + 1
NORMAL_ONE = 32767


@dataclass(frozen=True)
class Polygon:
    """A collision triangle. `corners` keep the flags in their top three bits."""

    surface: int
    corners: tuple[int, int, int]
    normal: tuple[int, int, int]
    distance: float
    # The format's remaining word, kept as read.
    word: int = 0

    def vertices(self) -> tuple[int, int, int]:
        return tuple(corner & VERTEX_INDEX for corner in self.corners)

    def passed_by_actors(self) -> Polygon:
        return replace(self, corners=(self.corners[0] | IGNORE_ENTITY, *self.corners[1:]))

    def facing(self, positions: list[tuple[int, int, int]], flags: Polygon) -> Polygon:
        """This triangle over `positions`, with its plane computed and `flags`' corner flags."""
        a, b, c = (positions[i] for i in self.vertices())
        u = [b[i] - a[i] for i in range(3)]
        v = [c[i] - a[i] for i in range(3)]
        n = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
        length = sum(x * x for x in n) ** 0.5
        unit = tuple(x / length for x in n)
        corners = tuple(i | (f & ~VERTEX_INDEX) for i, f in zip(self.vertices(), flags.corners))
        return Polygon(self.surface, corners, tuple(round(x * NORMAL_ONE) for x in unit),
                       -sum(x * y for x, y in zip(unit, a)), flags.word)


@dataclass(frozen=True)
class Scene:
    """A scene file's collision: its vertices (integers, world space) and triangles."""

    data: bytes
    header: int
    vertices: list[tuple[int, int, int]]
    polygons: list[Polygon]

    @classmethod
    def parse(cls, data: bytes) -> Scene:
        if data[0:4] not in MAGICS:
            raise ValueError("not a scene file")
        offset = BASE
        while True:
            code, word = struct.unpack_from("<BxxxI", data, offset)
            if code == COLLISION:
                break
            if code == END:
                raise ValueError("scene without collision")
            offset += 8
        header = BASE + word
        vertex_count, polygon_count = struct.unpack_from("<HH", data, header + COLLISION_COUNTS[data[0:4]])
        vertices_at, polygons_at = (BASE + v for v in struct.unpack_from("<II", data, header + COLLISION_VERTICES))
        vertices = [struct.unpack_from("<3h", data, vertices_at + i * VERTEX_SIZE) for i in range(vertex_count)]
        polygons = []
        for i in range(polygon_count):
            surface, a, b, c, *words, distance = struct.unpack_from("<4H4hf", data, polygons_at + i * POLYGON_SIZE)
            word = words.pop(0 if POLYGON_WORD_FIRST[data[0:4]] else 3)
            polygons.append(Polygon(surface, (a, b, c), tuple(words), distance, word))
        return cls(data, header, vertices, polygons)

    def with_collision(self, vertices: list[tuple[int, int, int]], polygons: list[Polygon]) -> bytes:
        """The scene with new vertex and triangle lists, written after everything else."""
        if len(vertices) > MAX_VERTICES:
            raise ValueError(f"{len(vertices)} collision vertices; a triangle can name {MAX_VERTICES}")
        out = bytearray(self.data)
        out.extend(bytes(-len(out) % 4))
        vertices_at = len(out)
        for vertex in vertices:
            out.extend(struct.pack("<3h", *vertex))
        out.extend(bytes(-len(out) % 4))
        polygons_at = len(out)
        for polygon in polygons:
            words = [*polygon.normal]
            words.insert(0 if POLYGON_WORD_FIRST[self.data[0:4]] else 3, polygon.word)
            out.extend(struct.pack("<4H4hf", polygon.surface, *polygon.corners, *words, polygon.distance))
        struct.pack_into("<HH", out, self.header + COLLISION_COUNTS[self.data[0:4]], len(vertices), len(polygons))
        struct.pack_into("<II", out, self.header + COLLISION_VERTICES, vertices_at - BASE, polygons_at - BASE)
        return bytes(out)
