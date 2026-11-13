"""OoT3D CMB models (version 6): textures, materials, meshes, vertices, and rebuilding one shape.

Chunk layout after noclip.website's src/OcarinaOfTime3D/cmb.ts (MIT, Jasper St. Pierre and
contributors).
"""

from __future__ import annotations

import struct
from dataclasses import dataclass

OCARINA = 6
# Index and attribute component types (GL enums).
COMPONENTS = {0x1400: ("b", 1), 0x1401: ("B", 1), 0x1402: ("h", 2), 0x1403: ("H", 2),
              0x1404: ("i", 4), 0x1405: ("I", 4), 0x1406: ("f", 4)}
MATERIAL_SIZE = 0x15C
ATTRIBUTE_ARRAY = 0
UNSIGNED_SHORT = 0x1403
# OoT3D sign-extends a primitive's 16-bit index count into the draw's vertex count.
MAX_PRIMITIVE_INDICES = 0x7FFF
# vatr slice order; a sepd's attributes come in the same order.
SLICES = ("position", "normal", "color", "tex0", "tex1", "tex2", "bone_indices", "bone_weights")
WIDTHS = {"position": 3, "normal": 3, "color": 4, "tex0": 2, "tex1": 2, "tex2": 2}
SEPD_ATTRIBUTES = 0x24
ATTRIBUTE_SIZE = 0x1C
# A sepd's primitive set table: a bone dimension word, then one u16 offset per prms chunk.
SEPD_TABLE = SEPD_ATTRIBUTES + 8 * ATTRIBUTE_SIZE
SEPD_COUNT = 0x08
CHUNK_SIZE = 0x04
SKLM = 0x30
# Header offsets of the chunks that follow the skeleton-mesh chunk.
LATER_CHUNKS = (0x34, 0x38, 0x3C, 0x40)
VATR_TABLE = 0x0C
VATR_DATA = VATR_TABLE + len(SLICES) * 8
INDEX_COUNT = 0x20
INDEX_CHUNK = 0x3C
TEXTURE_DATA = 0x40
CHUNK_ALIGN = 0x100


@dataclass(frozen=True)
class Texture:
    name: str
    width: int
    height: int
    format: int
    # Offset of the first level in the texture data chunk.
    offset: int
    size: int


@dataclass(frozen=True)
class Attribute:
    start: int
    scale: float
    data_type: int
    mode: int
    constant: tuple[float, float, float, float]


@dataclass(frozen=True)
class Primitive:
    index_type: int
    count: int
    # Byte offset into the index buffer.
    offset: int


@dataclass(frozen=True)
class Shape:
    """A sepd: one vertex layout and its primitive sets."""

    offset: int
    center: tuple[float, float, float]
    position: Attribute
    normal: Attribute
    color: Attribute
    tex0: Attribute
    tex1: Attribute
    tex2: Attribute
    primitives: list[Primitive]
    # Offset of each primitive's prm chunk.
    prm_chunks: list[int]

    def attribute(self, name: str) -> Attribute:
        return getattr(self, name)


@dataclass(frozen=True)
class Mesh:
    shape: int
    material: int


@dataclass(frozen=True)
class Cmb:
    data: bytes
    name: str
    textures: list[Texture]
    # Each material's texture indices (-1 for none), three binding slots.
    materials: list[tuple[int, int, int]]
    meshes: list[Mesh]
    shapes: list[Shape]
    # vatr slices: name -> (offset in data, size).
    vatr: dict[str, tuple[int, int]]
    index_offset: int

    @classmethod
    def parse(cls, data: bytes) -> Cmb:
        if data[0:4] != b"cmb ":
            raise ValueError("not a CMB")
        version = struct.unpack_from("<I", data, 0x08)[0]
        if version != OCARINA:
            raise ValueError(f"CMB version {version}, not OoT3D's {OCARINA}")
        name = data[0x10:0x20].split(b"\0")[0].decode("ascii")
        _skl, mats, tex, sklm, _luts, vatr, indices = struct.unpack_from("<7I", data, 0x24)
        return cls(data, name, _textures(data, tex), _materials(data, mats), *_sklm(data, sklm),
                   _vatr(data, vatr), indices)

    def texture_data(self, texture: Texture) -> bytes:
        base = struct.unpack_from("<I", self.data, TEXTURE_DATA)[0] + texture.offset
        return self.data[base:base + texture.size]

    def indices(self, shape: Shape) -> list[int]:
        out: list[int] = []
        for primitive in shape.primitives:
            code, _ = COMPONENTS[primitive.index_type]
            base = self.index_offset + primitive.offset
            out.extend(struct.unpack_from(f"<{primitive.count}{code}", self.data, base))
        return out

    def arrays(self, shape: Shape) -> list[str]:
        """The shape's attributes read per vertex; the rest are constants or unused."""
        return [name for name in WIDTHS
                if shape.attribute(name).mode == ATTRIBUTE_ARRAY and self.vatr[name][1]]

    def vertices(self, shape: Shape, name: str) -> list[tuple[float, ...]] | None:
        """Every vertex of the attribute from the shape's start, scaled; None unless an array."""
        if name not in self.arrays(shape):
            return None
        attribute = shape.attribute(name)
        code, size = COMPONENTS[attribute.data_type]
        width = WIDTHS[name]
        offset, length = self.vatr[name]
        count = (length - attribute.start) // (size * width)
        raw = struct.unpack_from(f"<{count * width}{code}", self.data, offset + attribute.start)
        scale = attribute.scale
        return [tuple(v * scale for v in raw[i:i + width]) for i in range(0, len(raw), width)]

    def triangles(self, mesh: Mesh) -> list[tuple[tuple[float, ...], ...]]:
        """The mesh's triangles as world-space positions."""
        shape = self.shapes[mesh.shape]
        positions = self.vertices(shape, "position")
        if positions is None:
            raise ValueError("constant positions")
        index = self.indices(shape)
        return [(positions[index[i]], positions[index[i + 1]], positions[index[i + 2]])
                for i in range(0, len(index) - 2, 3)]

    def with_shape(self, shape_index: int, vertices: dict[str, list[tuple[float, ...]]],
                   indices: list[int]) -> bytes:
        """The model with a shape's triangles replaced.

        The new vertices go after the end of each vatr slice at one common index, and the new
        indices after the index buffer, so every other shape keeps its data and offsets. Indices
        beyond one primitive's count are spread over copies of the shape's one primitive set.
        """
        shape = self.shapes[shape_index]
        sets = -(-len(indices) // (MAX_PRIMITIVE_INDICES // 3 * 3))
        if len(shape.prm_chunks) != sets:
            if len(shape.prm_chunks) != 1:
                raise ValueError(f"shape {shape_index} has {len(shape.prm_chunks)} primitive sets")
            return Cmb.parse(_with_primitive_sets(self.data, shape.offset, sets)).with_shape(
                shape_index, vertices, indices)
        arrays = self.arrays(shape)
        if set(arrays) != set(vertices):
            raise ValueError(f"shape {shape_index} has arrays {arrays}, got {sorted(vertices)}")
        count = len(next(iter(vertices.values())))
        base = 0
        for name in arrays:
            attribute = shape.attribute(name)
            stride = COMPONENTS[attribute.data_type][1] * WIDTHS[name]
            base = max(base, -(-(self.vatr[name][1] - attribute.start) // stride))
        if base + count > 0xFFFF:
            raise ValueError("new vertices do not fit 16-bit indices")

        vatr = struct.unpack_from("<I", self.data, 0x38)[0]
        slices = []
        for name in SLICES:
            offset, size = self.vatr[name]
            data = bytearray(self.data[offset:offset + size])
            if name in vertices:
                attribute = shape.attribute(name)
                code, width = COMPONENTS[attribute.data_type]
                stride = width * WIDTHS[name]
                data.extend(bytes(attribute.start + base * stride - len(data)))
                data.extend(_encode(vertices[name], attribute, code))
            data.extend(bytes(-len(data) % 4))
            slices.append(bytes(data))
        chunk = bytearray(self.data[vatr:vatr + VATR_DATA])
        cursor = VATR_DATA
        for i, data in enumerate(slices):
            struct.pack_into("<II", chunk, VATR_TABLE + i * 8, len(data), cursor if data else 0)
            cursor += len(data)
        # An empty slice points at the end of the chunk, as the game's own files do.
        for i, data in enumerate(slices):
            if not data:
                struct.pack_into("<I", chunk, VATR_TABLE + i * 8 + 4, cursor)
        chunk.extend(b"".join(slices))
        position_stride = COMPONENTS[shape.position.data_type][1] * WIDTHS["position"]
        struct.pack_into("<II", chunk, 4, len(chunk), len(slices[0]) // position_stride)

        index_count = struct.unpack_from("<I", self.data, INDEX_COUNT)[0]
        index_data = bytearray(self.data[self.index_offset:self.index_offset + index_count * 2])
        if index_count + len(indices) > 0xFFFF:
            raise ValueError("index buffer outgrows 16-bit primitive offsets")
        index_data.extend(struct.pack(f"<{len(indices)}H", *(base + i for i in indices)))
        texture_data = struct.unpack_from("<I", self.data, TEXTURE_DATA)[0]
        size = struct.unpack_from("<I", self.data, 4)[0]

        out = bytearray(self.data[:vatr])
        out.extend(chunk)
        out.extend(bytes(-len(out) % 4))
        new_index = len(out)
        out.extend(index_data)
        out.extend(bytes(-len(out) % CHUNK_ALIGN))
        new_texture_data = len(out)
        out.extend(self.data[texture_data:size])
        out.extend(bytes(-len(out) % CHUNK_ALIGN))
        struct.pack_into("<I", out, 4, len(out))
        struct.pack_into("<I", out, INDEX_COUNT, index_count + len(indices))
        struct.pack_into("<I", out, INDEX_CHUNK, new_index)
        struct.pack_into("<I", out, TEXTURE_DATA, new_texture_data)
        # Whole triangles per set, each under the game's signed count.
        per_set = -(-len(indices) // 3 // len(shape.prm_chunks)) * 3
        for i, prm in enumerate(shape.prm_chunks):
            count = len(indices[i * per_set:(i + 1) * per_set])
            struct.pack_into("<H", out, prm + 0x10, UNSIGNED_SHORT)
            struct.pack_into("<HH", out, prm + 0x14, count, index_count + i * per_set)
        return bytes(out)


def _with_primitive_sets(data: bytes, sepd: int, sets: int) -> bytes:
    """The model with a one-set sepd holding copies of its prms chunk; later chunks move."""
    old_size = struct.unpack_from("<I", data, sepd + CHUNK_SIZE)[0]
    table = sepd + SEPD_TABLE + 4
    prms = sepd + struct.unpack_from("<H", data, table)[0]
    prms_chunk = data[prms:prms + struct.unpack_from("<I", data, prms + CHUNK_SIZE)[0]]
    first = SEPD_TABLE + 4 + sets * 2
    first += -first % 4
    new = bytearray(data[sepd:sepd + SEPD_TABLE + 4])
    new.extend(struct.pack(f"<{sets}H", *(first + i * len(prms_chunk) for i in range(sets))))
    new.extend(bytes(first - len(new)))
    new.extend(prms_chunk * sets)
    # Pad so every later chunk keeps its alignment.
    new.extend(bytes(-(len(new) - old_size) % CHUNK_ALIGN))
    grow = len(new) - old_size
    struct.pack_into("<I", new, CHUNK_SIZE, len(new))
    struct.pack_into("<H", new, SEPD_COUNT, sets)
    out = bytearray(data[:sepd] + new + data[sepd + old_size:])

    sklm = struct.unpack_from("<I", data, SKLM)[0]
    shp = sklm + struct.unpack_from("<I", data, sklm + 0x0C)[0]
    for chunk in (sklm, shp):
        struct.pack_into("<I", out, chunk + CHUNK_SIZE, struct.unpack_from("<I", data, chunk + CHUNK_SIZE)[0] + grow)
    for i in range(struct.unpack_from("<I", data, shp + 0x08)[0]):
        at = shp + 0x10 + i * 2
        offset = struct.unpack_from("<H", data, at)[0]
        if shp + offset > sepd:
            struct.pack_into("<H", out, at, offset + grow)
    for at in (CHUNK_SIZE, *LATER_CHUNKS):
        struct.pack_into("<I", out, at, struct.unpack_from("<I", data, at)[0] + grow)
    return bytes(out)


def _encode(values: list[tuple[float, ...]], attribute: Attribute, code: str) -> bytes:
    if code == "f":
        flat = [v / attribute.scale for value in values for v in value]
    else:
        low, high = {"b": (-128, 127), "B": (0, 255), "h": (-32768, 32767), "H": (0, 65535)}[code]
        flat = [min(high, max(low, round(v / attribute.scale))) for value in values for v in value]
    return struct.pack(f"<{len(flat)}{code}", *flat)


def _textures(data: bytes, offset: int) -> list[Texture]:
    count = struct.unpack_from("<I", data, offset + 0x08)[0]
    textures = []
    for i in range(count):
        entry = offset + 0x0C + i * 0x24
        size, _, width, height, gl_format, data_offset = struct.unpack_from("<IIHHII", data, entry)
        name = data[entry + 0x14: entry + 0x24].split(b"\0")[0].decode("ascii")
        textures.append(Texture(name, width, height, gl_format, data_offset, size))
    return textures


def _materials(data: bytes, offset: int) -> list[tuple[int, int, int]]:
    count = struct.unpack_from("<I", data, offset + 0x08)[0]
    out = []
    for i in range(count):
        entry = offset + 0x0C + i * MATERIAL_SIZE
        out.append(tuple(struct.unpack_from("<h", data, entry + 0x10 + j * 0x18)[0] for j in range(3)))
    return out


def _attribute(data: bytes, offset: int) -> Attribute:
    start, scale, data_type, mode = struct.unpack_from("<IfHH", data, offset)
    return Attribute(start, scale, data_type, mode, struct.unpack_from("<4f", data, offset + 0x0C))


def _sklm(data: bytes, offset: int) -> tuple[list[Mesh], list[Shape]]:
    mshs, shp = struct.unpack_from("<II", data, offset + 0x08)
    mshs += offset
    shp += offset
    meshes = []
    for i in range(struct.unpack_from("<I", data, mshs + 0x08)[0]):
        shape, material = struct.unpack_from("<HB", data, mshs + 0x10 + i * 4)
        meshes.append(Mesh(shape, material))
    shapes = []
    for i in range(struct.unpack_from("<I", data, shp + 0x08)[0]):
        sepd = shp + struct.unpack_from("<H", data, shp + 0x10 + i * 2)[0]
        shapes.append(_shape(data, sepd))
    return meshes, shapes


def _shape(data: bytes, sepd: int) -> Shape:
    if data[sepd:sepd + 4] != b"sepd":
        raise ValueError("bad sepd")
    count = struct.unpack_from("<H", data, sepd + 0x08)[0]
    center = struct.unpack_from("<3f", data, sepd + 0x0C)
    attributes = [_attribute(data, sepd + SEPD_ATTRIBUTES + i * ATTRIBUTE_SIZE) for i in range(8)]
    table = sepd + SEPD_ATTRIBUTES + 8 * ATTRIBUTE_SIZE + 4
    primitives = []
    prm_chunks = []
    for i in range(count):
        prms = sepd + struct.unpack_from("<H", data, table + i * 2)[0]
        prm = prms + struct.unpack_from("<I", data, prms + 0x14)[0]
        index_type, _, _, count_, offset = struct.unpack_from("<hbbHH", data, prm + 0x10)
        primitives.append(Primitive(index_type, count_, offset * 2))
        prm_chunks.append(prm)
    position, normal, color, tex0, tex1, tex2 = attributes[:6]
    return Shape(sepd, center, position, normal, color, tex0, tex1, tex2, primitives, prm_chunks)


def _vatr(data: bytes, offset: int) -> dict[str, tuple[int, int]]:
    out = {}
    for i, name in enumerate(SLICES):
        size, start = struct.unpack_from("<II", data, offset + 0x0C + i * 8)
        out[name] = (offset + start, size)
    return out
