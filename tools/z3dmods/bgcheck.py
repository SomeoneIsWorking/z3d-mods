"""The room a scene's collision has in the game: BgCheck_Allocate's grid and memory for a scene,
the list nodes its triangles take in that grid, and the game's limits on them
(decomp/oot3d docs/notes/scene_collision.md, decomp/mm3d docs/formats/scene_files.md).
"""

from __future__ import annotations

import struct
from dataclasses import dataclass

from z3dmods import symbols, zsi
from z3dmods.patch_build import CODE_BASE

SCENE_PATH_PREFIX = "rom:/"


@dataclass(frozen=True)
class Budget:
    """What BgCheck_Allocate gives one scene."""

    # Cells along x, y and z.
    amounts: tuple[int, int, int]
    memory: int
    # The moving collision's list nodes, triangles and vertices, taken from the same memory.
    dyna: tuple[int, int, int]
    # A list length the game's tables fix for the scene; 0 when it follows from the memory.
    fixed_nodes: int = 0


@dataclass(frozen=True)
class Use:
    # The triangle and cell pairs that touch, and every pair the game tests: its own test lets
    # a few more through than touch (0.3% in the scenes read in game), never more than it tests.
    nodes: int
    nodes_at_most: int
    node_limit: int
    polygons: int
    polygon_limit: int

    def over(self) -> str | None:
        """What is over its limit, in words; None when the scene fits."""
        if self.polygons > self.polygon_limit:
            return f"{self.polygons} collision triangles; the game's lists name {self.polygon_limit}"
        if self.nodes_at_most > self.node_limit:
            return (f"about {self.nodes} and at most {self.nodes_at_most} collision list nodes; "
                    f"the game has room for {self.node_limit}")
        return None

    def __str__(self) -> str:
        return (f"{self.nodes} of {self.node_limit} list nodes ({100 * self.nodes // self.node_limit}%, "
                f"at most {self.nodes_at_most}), {self.polygons} of {self.polygon_limit} triangles")


class Rules:
    """A game's BgCheck_Allocate tables, read from its code at the decomp's addresses."""

    def __init__(self, game: str, decomp: symbols.Decomp, code: bytes) -> None:
        self._game = game
        self._d = decomp
        self._code = code

    def _define(self, name: str) -> int:
        return self._d.define(name)

    def _entries(self, table: str, count: str, entry: str | None, layout: str) -> list[tuple]:
        size = symbols.size(self._d, entry) if entry else struct.calcsize(layout)
        start = symbols.global_(self._d, table) - CODE_BASE
        return [struct.unpack_from(layout, self._code, start + i * size) for i in range(self._define(count))]

    def scene_ids(self) -> dict[str, int]:
        """Scene id by the scene file's path in the ROM."""
        size = symbols.size(self._d, "SceneTableEntry")
        path_at = symbols.offset(self._d, "SceneTableEntry.infoPath")
        ids: dict[str, int] = {}
        tables = [("gSceneTable", "SCENE_TABLE_COUNT")]
        if "gDungeonSceneTable" in self._d.globals:
            tables.append(("gDungeonSceneTable", "SCENE_TABLE_DUNGEON_COUNT"))
        for table, count in tables:
            start = symbols.global_(self._d, table) - CODE_BASE
            for scene in range(self._define(count)):
                at = start + scene * size + path_at
                path = self._code[at:self._code.index(b"\0", at)].decode()
                if path.startswith(SCENE_PATH_PREFIX):
                    ids.setdefault(path[len(SCENE_PATH_PREFIX):], scene)
        return ids

    def budget(self, scene_id: int, camera_type: int) -> Budget:
        spots = {scene for scene, in self._entries("sBgCheckSpotScenes", "BGCHECK_SPOT_SCENE_COUNT", None, "<h")}
        subdivisions = {scene: ((x, y, z), max(nodes, 0)) for scene, x, y, z, nodes in self._entries(
            "sBgCheckSceneSubdiv", "BGCHECK_SCENE_SUBDIV_COUNT", "BgCheckSceneSubdiv", "<4hi")}
        default = (self._define("BGCHECK_SUBDIV_XZ"), self._define("BGCHECK_SUBDIV_Y"), self._define("BGCHECK_SUBDIV_XZ"))
        dyna = tuple(self._define(f"BGCHECK_DYNA_{part}") for part in ("NODES", "POLYS", "VTX"))
        spot = Budget((self._define("BGCHECK_SPOT_SUBDIV_XZ"), self._define("BGCHECK_SPOT_SUBDIV_Y"),
                       self._define("BGCHECK_SPOT_SUBDIV_XZ")), 0,
                      tuple(self._define(f"BGCHECK_SPOT_DYNA_{part}") for part in ("NODES", "POLYS", "VTX")))
        amounts, fixed_nodes = subdivisions.get(scene_id, (default, 0))
        if self._game == "mm3d":
            memory = self._define("BGCHECK_MEMSIZE")
            if scene_id in spots:
                return Budget(spot.amounts, memory, spot.dyna)
            for scene, *maxima in self._entries("sBgCheckSceneDyna", "BGCHECK_SCENE_DYNA_COUNT", "BgCheckSceneDyna", "<4h"):
                if scene == scene_id:
                    dyna = tuple(maxima)
            return Budget(amounts, memory, dyna, fixed_nodes)
        if camera_type in {self._define(f"SCENE_CAM_TYPE_{name}") for name in
                           ("FIXED_SHOP_VIEWPOINT", "FIXED_TOGGLE_VIEWPOINT", "FIXED", "FIXED_MARKET")}:
            stable = scene_id == self._define("SCENE_STABLE")
            side = self._define("BGCHECK_FIXED_CAM_SUBDIV")
            return Budget((side, side, side),
                          self._define("BGCHECK_FIXED_CAM_STABLE_MEMSIZE" if stable else "BGCHECK_FIXED_CAM_MEMSIZE"),
                          tuple(self._define(f"BGCHECK_FIXED_CAM_DYNA_{part}") for part in ("NODES", "POLYS", "VTX")))
        if scene_id in spots:
            return Budget(spot.amounts, self._define("BGCHECK_SPOT_MEMSIZE"), spot.dyna)
        memories = dict(self._entries("sBgCheckSceneMem", "BGCHECK_SCENE_MEM_COUNT", "BgCheckSceneMem", "<hxxI"))
        return Budget(amounts, memories.get(scene_id, self._define("BGCHECK_MEMSIZE")), dyna, fixed_nodes)

    def node_limit(self, budget: Budget, polygons: int) -> int:
        """The list nodes a scene with `polygons` triangles can use: what is left of its memory,
        and no more than a 16 bit node index can name."""
        cells = budget.amounts[0] * budget.amounts[1] * budget.amounts[2]
        taken = (cells * self._define("BGCHECK_LOOKUP_SIZE") + polygons
                 + budget.dyna[0] * self._define("BGCHECK_SSNODE_SIZE")
                 + budget.dyna[1] * self._define("BGCHECK_DYNA_POLY_SIZE")
                 + budget.dyna[2] * self._define("BGCHECK_DYNA_VTX_SIZE") + self._define("BGCHECK_CONTEXT_SIZE"))
        room = budget.fixed_nodes or max(budget.memory - taken, 0) // self._define("BGCHECK_SSNODE_SIZE")
        return min(room, self._define("BGCHECK_SSNODE_NULL"))

    def nodes(self, scene: zsi.Scene, amounts: tuple[int, int, int]) -> tuple[int, int]:
        """The list nodes BgCheck_InitializeStaticLookup takes, one per triangle and cell it
        reaches, a cell being widened by the overlap on every side: the pairs that touch, and
        all the pairs it tests."""
        overlap = self._define("BGCHECK_SUBDIV_OVERLAP")
        low, high = scene.bounds
        lengths = [max(int((high[axis] - low[axis]) / amounts[axis]) + 1, self._define("BGCHECK_SUBDIV_MIN"))
                   for axis in range(3)]
        half = [length / 2 + overlap for length in lengths]
        total = tested = 0
        for polygon in scene.polygons:
            corners = [scene.vertices[i] for i in polygon.vertices()]
            first, last = [], []
            for axis in range(3):
                near = min(corner[axis] for corner in corners) - low[axis]
                far = max(corner[axis] for corner in corners) - low[axis]
                start, end = near // lengths[axis], far // lengths[axis]
                if near % lengths[axis] < overlap and start > 0:
                    start -= 1
                if lengths[axis] - overlap < far % lengths[axis] and end < amounts[axis] - 1:
                    end += 1
                first.append(max(start, 0))
                last.append(min(end, amounts[axis] - 1))
            for x in range(first[0], last[0] + 1):
                for y in range(first[1], last[1] + 1):
                    for z in range(first[2], last[2] + 1):
                        centre = [low[axis] + lengths[axis] * (cell + 0.5) for axis, cell in enumerate((x, y, z))]
                        total += _triangle_meets_box(corners, centre, half)
                        tested += 1
        return total, tested

    def use(self, scene: zsi.Scene, scene_id: int) -> Use:
        budget = self.budget(scene_id, scene.camera_type_at_collision)
        return Use(*self.nodes(scene, budget.amounts), self.node_limit(budget, len(scene.polygons)),
                   len(scene.polygons), self._define("BGCHECK_POLY_ID_MAX") + 1)


def _triangle_meets_box(corners, centre, half) -> bool:
    """Whether a triangle touches an axis-aligned box (separating axes, Akenine-Moller)."""
    v = [[corner[axis] - centre[axis] for axis in range(3)] for corner in corners]
    for axis in range(3):
        if min(p[axis] for p in v) > half[axis] or max(p[axis] for p in v) < -half[axis]:
            return False
    edges = [[v[(i + 1) % 3][axis] - v[i][axis] for axis in range(3)] for i in range(3)]
    normal = (edges[0][1] * edges[1][2] - edges[0][2] * edges[1][1],
              edges[0][2] * edges[1][0] - edges[0][0] * edges[1][2],
              edges[0][0] * edges[1][1] - edges[0][1] * edges[1][0])
    if abs(sum(n * p for n, p in zip(normal, v[0]))) > sum(abs(n) * h for n, h in zip(normal, half)):
        return False
    for edge in edges:
        for axis in range(3):
            a, b = (axis + 1) % 3, (axis + 2) % 3
            # The box axis crossed with the edge.
            along = [-edge[b] * p[a] + edge[a] * p[b] for p in v]
            reach = abs(edge[b]) * half[a] + abs(edge[a]) * half[b]
            if min(along) > reach or max(along) < -reach:
                return False
    return True
