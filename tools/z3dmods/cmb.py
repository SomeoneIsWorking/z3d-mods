"""OoT3D (version 6) and MM3D (version 10) CMB models: textures, materials, meshes, vertices, and
rebuilding one shape.

Chunk layout after noclip.website's src/OcarinaOfTime3D/cmb.ts (MIT, Jasper St. Pierre and
contributors).
"""

from __future__ import annotations

import struct
from dataclasses import dataclass

OCARINA = 6
MAJORA = 10
# Index and attribute component types (GL enums).
COMPONENTS = {0x1400: ("b", 1), 0x1401: ("B", 1), 0x1402: ("h", 2), 0x1403: ("H", 2),
              0x1404: ("i", 4), 0x1405: ("I", 4), 0x1406: ("f", 4)}
ATTRIBUTE_ARRAY = 0
UNSIGNED_SHORT = 0x1403
# OoT3D sign-extends a primitive's 16-bit index count into the draw's vertex count.
MAX_PRIMITIVE_INDICES = 0x7FFF
WIDTHS = {"position": 3, "normal": 3, "tangent": 3, "color": 4, "tex0": 2, "tex1": 2, "tex2": 2}
SEPD_ATTRIBUTES = 0x24
ATTRIBUTE_SIZE = 0x1C
SEPD_COUNT = 0x08
CHUNK_SIZE = 0x04
VATR_TABLE = 0x0C
INDEX_COUNT = 0x20
CHUNK_ALIGN = 0x100


@dataclass(frozen=True)
class Layout:
    """What differs between the versions."""

    # vatr slice order; a sepd's attributes come in the same order.
    slices: tuple[str, ...]
    material_size: int
    mesh_size: int
    # Header offsets of the chunk pointers.
    materials: int
    textures: int
    sklm: int
    vatr: int
    index: int
    texture_data: int

    @property
    def sepd_table(self) -> int:
        """A sepd's primitive set table: a bone dimension word, then one u16 offset per prms chunk."""
        return SEPD_ATTRIBUTES + len(self.slices) * ATTRIBUTE_SIZE

    @property
    def vatr_data(self) -> int:
        return VATR_TABLE + len(self.slices) * 8

    @property
    def later_chunks(self) -> tuple[int, ...]:
        """Header offsets of the chunks that follow the skeleton-mesh chunk."""
        return tuple(range(self.sklm + 4, self.texture_data + 4, 4))


# MM3D adds a qtrs chunk pointer after the skeleton's, a tangent slice, a stencil block per
# material and a wider mesh entry.
LAYOUTS = {
    OCARINA: Layout(("position", "normal", "color", "tex0", "tex1", "tex2", "bone_indices", "bone_weights"),
                    0x15C, 0x04, 0x28, 0x2C, 0x30, 0x38, 0x3C, 0x40),
    MAJORA: Layout(("position", "normal", "tangent", "color", "tex0", "tex1", "tex2", "bone_indices",
                    "bone_weights"), 0x16C, 0x0C, 0x2C, 0x30, 0x34, 0x3C, 0x40, 0x44),
}


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
    # By slice name.
    attributes: dict[str, Attribute]
    primitives: list[Primitive]
    # Offset of each primitive's prm chunk.
    prm_chunks: list[int]

    def attribute(self, name: str) -> Attribute:
        return self.attributes[name]


@dataclass(frozen=True)
class Mesh:
    shape: int
    material: int


@dataclass(frozen=True)
class Cmb:
    data: bytes
    layout: Layout
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
        layout = LAYOUTS.get(version)
        if layout is None:
            raise ValueError(f"CMB version {version}, not one of {sorted(LAYOUTS)}")
        name = data[0x10:0x20].split(b"\0")[0].decode("ascii")

        def chunk(at: int) -> int:
            return struct.unpack_from("<I", data, at)[0]

        return cls(data, layout, name, texture_table(data, chunk(layout.textures)),
                   _materials(data, chunk(layout.materials), layout),
                   *_sklm(data, chunk(layout.sklm), layout), _vatr(data, chunk(layout.vatr), layout),
                   chunk(layout.index))

    def texture_data(self, texture: Texture) -> bytes | None:
        """The texture's pixels; None when the model keeps them outside (MM3D's .ctxb bundles)."""
        start = struct.unpack_from("<I", self.data, self.layout.texture_data)[0]
        if start == 0:
            return None
        base = start + texture.offset
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
        return [name for name in self.layout.slices
                if name in WIDTHS and shape.attribute(name).mode == ATTRIBUTE_ARRAY and self.vatr[name][1]]

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
            return Cmb.parse(_with_primitive_sets(self.data, self.layout, shape.offset, sets)).with_shape(
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

        layout = self.layout
        vatr = struct.unpack_from("<I", self.data, layout.vatr)[0]
        slices = []
        for name in layout.slices:
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
        chunk = bytearray(self.data[vatr:vatr + layout.vatr_data])
        cursor = layout.vatr_data
        for i, data in enumerate(slices):
            struct.pack_into("<II", chunk, VATR_TABLE + i * 8, len(data), cursor if data else 0)
            cursor += len(data)
        # An empty slice points at the end of the chunk, as the game's own files do.
        for i, data in enumerate(slices):
            if not data:
                struct.pack_into("<I", chunk, VATR_TABLE + i * 8 + 4, cursor)
        chunk.extend(b"".join(slices))
        position_stride = COMPONENTS[shape.attribute("position").data_type][1] * WIDTHS["position"]
        struct.pack_into("<II", chunk, 4, len(chunk), len(slices[0]) // position_stride)

        index_count = struct.unpack_from("<I", self.data, INDEX_COUNT)[0]
        index_data = bytearray(self.data[self.index_offset:self.index_offset + index_count * 2])
        # Whole triangles per set, each under the game's signed count.
        per_set = -(-len(indices) // 3 // len(shape.prm_chunks)) * 3
        # A set names its first index in 16 bits.
        if index_count + (len(shape.prm_chunks) - 1) * per_set > 0xFFFF:
            raise ValueError("index buffer outgrows 16-bit primitive offsets")
        index_data.extend(struct.pack(f"<{len(indices)}H", *(base + i for i in indices)))
        texture_data = struct.unpack_from("<I", self.data, layout.texture_data)[0]
        size = struct.unpack_from("<I", self.data, 4)[0]

        out = bytearray(self.data[:vatr])
        out.extend(chunk)
        out.extend(bytes(-len(out) % 4))
        new_index = len(out)
        out.extend(index_data)
        out.extend(bytes(-len(out) % CHUNK_ALIGN))
        if texture_data:
            struct.pack_into("<I", out, layout.texture_data, len(out))
            out.extend(self.data[texture_data:size])
            out.extend(bytes(-len(out) % CHUNK_ALIGN))
        struct.pack_into("<I", out, 4, len(out))
        struct.pack_into("<I", out, INDEX_COUNT, index_count + len(indices))
        struct.pack_into("<I", out, layout.index, new_index)
        for i, prm in enumerate(shape.prm_chunks):
            count = len(indices[i * per_set:(i + 1) * per_set])
            struct.pack_into("<H", out, prm + 0x10, UNSIGNED_SHORT)
            struct.pack_into("<HH", out, prm + 0x14, count, index_count + i * per_set)
        return bytes(out)


def _with_primitive_sets(data: bytes, layout: Layout, sepd: int, sets: int) -> bytes:
    """The model with a one-set sepd holding copies of its prms chunk; later chunks move."""
    old_size = struct.unpack_from("<I", data, sepd + CHUNK_SIZE)[0]
    table = sepd + layout.sepd_table + 4
    prms = sepd + struct.unpack_from("<H", data, table)[0]
    prms_chunk = data[prms:prms + struct.unpack_from("<I", data, prms + CHUNK_SIZE)[0]]
    first = layout.sepd_table + 4 + sets * 2
    first += -first % 4
    new = bytearray(data[sepd:sepd + layout.sepd_table + 4])
    new.extend(struct.pack(f"<{sets}H", *(first + i * len(prms_chunk) for i in range(sets))))
    new.extend(bytes(first - len(new)))
    new.extend(prms_chunk * sets)
    # Pad so every later chunk keeps its alignment.
    new.extend(bytes(-(len(new) - old_size) % CHUNK_ALIGN))
    grow = len(new) - old_size
    struct.pack_into("<I", new, CHUNK_SIZE, len(new))
    struct.pack_into("<H", new, SEPD_COUNT, sets)
    out = bytearray(data[:sepd] + new + data[sepd + old_size:])

    sklm = struct.unpack_from("<I", data, layout.sklm)[0]
    shp = sklm + struct.unpack_from("<I", data, sklm + 0x0C)[0]
    for chunk in (sklm, shp):
        struct.pack_into("<I", out, chunk + CHUNK_SIZE, struct.unpack_from("<I", data, chunk + CHUNK_SIZE)[0] + grow)
    for i in range(struct.unpack_from("<I", data, shp + 0x08)[0]):
        at = shp + 0x10 + i * 2
        offset = struct.unpack_from("<H", data, at)[0]
        if shp + offset > sepd:
            struct.pack_into("<H", out, at, offset + grow)
    for at in (CHUNK_SIZE, *layout.later_chunks):
        value = struct.unpack_from("<I", data, at)[0]
        # A model without its own texture data has no such chunk to move.
        struct.pack_into("<I", out, at, value + grow if value else 0)
    return bytes(out)


def _encode(values: list[tuple[float, ...]], attribute: Attribute, code: str) -> bytes:
    if code == "f":
        flat = [v / attribute.scale for value in values for v in value]
    else:
        low, high = {"b": (-128, 127), "B": (0, 255), "h": (-32768, 32767), "H": (0, 65535)}[code]
        flat = [min(high, max(low, round(v / attribute.scale))) for value in values for v in value]
    return struct.pack(f"<{len(flat)}{code}", *flat)


def texture_table(data: bytes, offset: int) -> list[Texture]:
    count = struct.unpack_from("<I", data, offset + 0x08)[0]
    textures = []
    for i in range(count):
        entry = offset + 0x0C + i * 0x24
        size, _, width, height, gl_format, data_offset = struct.unpack_from("<IIHHII", data, entry)
        name = data[entry + 0x14: entry + 0x24].split(b"\0")[0].decode("ascii")
        textures.append(Texture(name, width, height, gl_format, data_offset, size))
    return textures


def _materials(data: bytes, offset: int, layout: Layout) -> list[tuple[int, int, int]]:
    count = struct.unpack_from("<I", data, offset + 0x08)[0]
    out = []
    for i in range(count):
        entry = offset + 0x0C + i * layout.material_size
        out.append(tuple(struct.unpack_from("<h", data, entry + 0x10 + j * 0x18)[0] for j in range(3)))
    return out


def _attribute(data: bytes, offset: int) -> Attribute:
    start, scale, data_type, mode = struct.unpack_from("<IfHH", data, offset)
    return Attribute(start, scale, data_type, mode, struct.unpack_from("<4f", data, offset + 0x0C))


def _sklm(data: bytes, offset: int, layout: Layout) -> tuple[list[Mesh], list[Shape]]:
    mshs, shp = struct.unpack_from("<II", data, offset + 0x08)
    mshs += offset
    shp += offset
    meshes = []
    for i in range(struct.unpack_from("<I", data, mshs + 0x08)[0]):
        shape, material = struct.unpack_from("<HB", data, mshs + 0x10 + i * layout.mesh_size)
        meshes.append(Mesh(shape, material))
    shapes = []
    for i in range(struct.unpack_from("<I", data, shp + 0x08)[0]):
        sepd = shp + struct.unpack_from("<H", data, shp + 0x10 + i * 2)[0]
        shapes.append(_shape(data, sepd, layout))
    return meshes, shapes


def _shape(data: bytes, sepd: int, layout: Layout) -> Shape:
    if data[sepd:sepd + 4] != b"sepd":
        raise ValueError("bad sepd")
    count = struct.unpack_from("<H", data, sepd + 0x08)[0]
    center = struct.unpack_from("<3f", data, sepd + 0x0C)
    attributes = {name: _attribute(data, sepd + SEPD_ATTRIBUTES + i * ATTRIBUTE_SIZE)
                  for i, name in enumerate(layout.slices)}
    table = sepd + layout.sepd_table + 4
    primitives = []
    prm_chunks = []
    for i in range(count):
        prms = sepd + struct.unpack_from("<H", data, table + i * 2)[0]
        prm = prms + struct.unpack_from("<I", data, prms + 0x14)[0]
        index_type, _, _, count_, offset = struct.unpack_from("<hbbHH", data, prm + 0x10)
        primitives.append(Primitive(index_type, count_, offset * 2))
        prm_chunks.append(prm)
    return Shape(sepd, center, attributes, primitives, prm_chunks)


def _vatr(data: bytes, offset: int, layout: Layout) -> dict[str, tuple[int, int]]:
    out = {}
    for i, name in enumerate(layout.slices):
        size, start = struct.unpack_from("<II", data, offset + 0x0C + i * 8)
        out[name] = (offset + start, size)
    return out
