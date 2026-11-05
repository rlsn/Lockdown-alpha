"""Single approved-world producer and closed polygon contract; no runtime navigation state."""
from collections.abc import Mapping
from contextlib import contextmanager
from dataclasses import dataclass
from fractions import Fraction
import heapq
import math
from pathlib import Path
from types import MappingProxyType

from CatalogTools import encode
import DemoContent as content


SCHEMA_VERSION = 2
PROJECTION_REVISION = 2
SOURCE = content.WORLD_SOURCE
SCHEMA_SOURCE = content.WORLD_SCHEMA_SOURCE
OUTPUT_PATHS = (content.WORLD_OUTPUT, content.WORLD_CONTRACT)
PARAMETER_REFS = (
    "AIRDROP.PickupRadiusCm", "CAMP.ResetArrivalRadiusCm", "CAPTURE.RadiusCm", "DORM.RadiusCm", "SHOP.InteractRadiusCm",
    "TIMING.DefendRouteBudgetSec", "UNIT.BasicAttackRangeCm", "UNIT.CollisionRadiusCm",
    "UNIT.MoveSpeedCmPerSec", "VISION.RadiusCm",
)
# D21 (#79): a Dorm is a public recovery disk of DORM.RadiusCm; D26 (#79): an Airdrop is a public drop point whose
# pickup disk is AIRDROP.PickupRadiusCm. Neither adds collision, navigation or sight blocking.
# D26 (#79): a BossWaypoint is one of the four public points the 画饼老板 Boss patrols in id order; a point only.
ANCHOR_KINDS = ("Spawn", "Camp", "Shop", "Capture", "Dorm", "Airdrop", "BossWaypoint")
ROOT_KEYS = {
    "schema_version", "projection_revision", "map_id", "layout_revision", "geometry_revision",
    "source_sha256", "author_schema_sha256", "approval", "geometry", "placement",
    "connections", "parameter_refs", "anchors",
}
AUTHOR_HEADER_FIELDS = (
    "schema_version", "projection_revision", "map_id", "layout_revision",
    "geometry_revision", "approval", "connections", "parameter_refs",
)
require = content.require
keys = content.keys
integer = content.integer
identifier = content.definition_id


def bounded(value, path="world geometry"):
    value = Fraction(value)
    integer(value.numerator, path, content.INT64_MIN)
    integer(value.denominator, path, 1)
    return value


def pair(value):
    value = bounded(value)
    return {"numerator": value.numerator, "denominator": value.denominator}


def vector(value):
    return {axis: pair(value[axis]) for axis in ("x", "y", "z")}


def point(value, path):
    return content._vector(value, path)


def sub(a, b):
    return tuple(bounded(x - y) for x, y in zip(a, b))


def dot(a, b):
    total = Fraction(0)
    for x, y in zip(a, b):
        total = bounded(total + bounded(x * y))
    return total


def cross(a, b):
    return bounded(bounded(a[0] * b[1]) - bounded(a[1] * b[0]))


def squared_distance(a, b):
    delta = sub(a[:2], b[:2])
    return dot(delta, delta)


def edges(polygon):
    return zip(polygon, polygon[1:] + polygon[:1])


def inside(p, polygon, strict=False):
    turns = [cross(sub(b, a), sub(p, a)) for a, b in edges(polygon)]
    return all(turn > 0 for turn in turns) if strict else all(turn >= 0 for turn in turns)


def segment_distance_squared(p, a, b):
    delta = sub(b[:2], a[:2])
    length = dot(delta, delta)
    require(length > 0, "InvalidGeometry", "edge", "zero length edge")
    numerator = dot(sub(p[:2], a[:2]), delta)
    if numerator <= 0:
        return squared_distance(p, a)
    if numerator >= length:
        return squared_distance(p, b)
    perpendicular = cross(delta, sub(p[:2], a[:2]))
    return bounded(bounded(perpendicular * perpendicular) / length)


def polygon_distance_squared(p, polygon):
    if inside(p, polygon):
        return Fraction(0)
    return min(segment_distance_squared(p, a, b) for a, b in edges(polygon))


def polygon_gap_squared(first, second):
    return min(
        *(polygon_distance_squared(p, second) for p in first),
        *(polygon_distance_squared(p, first) for p in second),
    )


def validate_polygon(polygon, path):
    require(len(polygon) == 4 and len(set(polygon)) == 4,
            "InvalidGeometry", path, "four distinct polygon vertices are required")
    require(all(cross(sub(polygon[(i + 1) % 4], polygon[i]),
                      sub(polygon[(i + 2) % 4], polygon[(i + 1) % 4])) > 0 for i in range(4)),
            "InvalidGeometry", path, "polygon must be strictly convex, simple and counterclockwise")
    area = sum((cross(a[:2], b[:2]) for a, b in edges(polygon)), Fraction(0))
    require(bounded(area) > 0, "InvalidGeometry", path, "polygon has no positive area")


def _approval(value):
    keys(value, {"status", "decision"}, "world.approval")
    require(value["status"] == "approved" and type(value["decision"]) is str
            and 0 < len(value["decision"]) <= 512 and value["decision"].strip(),
            "ApprovalRequired", "world.approval", "explicit reviewed world approval is required")
    try:
        value["decision"].encode("utf-8")
    except UnicodeEncodeError as error:
        raise content.ContentError("InvalidString", "world.approval", "invalid UTF-8 scalar") from error


def _author_fields(value, fields, path):
    require(type(value) is dict, "AuthorShape", path, "expected an authored object")
    expected = set(fields)
    require(set(value) == expected, "AuthorField", path,
            f"unconsumed fields {sorted(set(value) - expected)}; missing fields {sorted(expected - set(value))}")


def _author_rows(value, path, minimum, maximum=None):
    maximum = minimum if maximum is None else maximum
    require(type(value) is list and minimum <= len(value) <= maximum,
            "AuthorShape", path, f"expected an array with {minimum}..{maximum} entries")
    return enumerate(value)


def _author_vector(value, path):
    _author_fields(value, ("x", "y", "z"), path)
    for axis in ("x", "y", "z"):
        integer(value[axis], f"{path}.{axis}", content.INT64_MIN)


def validate_author_contract(author):
    """Code-owned producer vocabulary; editable schema may only further constrain it.

    Schema title/description annotations are provenance-only. They cannot add author
    fields or redefine units, scalar representations, or native node structures.
    """
    path = "world author"
    _author_fields(author, (*AUTHOR_HEADER_FIELDS, "coordinate_unit", "geometry", "placement", "anchors"), path)
    integer(author["schema_version"], path + ".schema_version", SCHEMA_VERSION, SCHEMA_VERSION)
    integer(author["projection_revision"], path + ".projection_revision", PROJECTION_REVISION, PROJECTION_REVISION)
    for field in ("layout_revision", "geometry_revision"):
        integer(author[field], f"{path}.{field}", 1, (1 << 32) - 1)
    identifier(author["map_id"], path + ".map_id")
    _author_fields(author["approval"], ("status", "decision"), path + ".approval")
    _approval(author["approval"])
    require(type(author["coordinate_unit"]) is str and author["coordinate_unit"] == "cm",
            "WorldUnit", path + ".coordinate_unit", "the native producer supports centimeters only")
    require(type(author["parameter_refs"]) is list and tuple(author["parameter_refs"]) == PARAMETER_REFS,
            "InvalidReference", path + ".parameter_refs", "unsupported central parameter references")

    geometry = author["geometry"]
    _author_fields(geometry, ("surfaces", "blockers", "sight_sample_height_cm", "camp_clearance_cm"), path + ".geometry")
    for field in ("sight_sample_height_cm", "camp_clearance_cm"):
        integer(geometry[field], f"{path}.geometry.{field}", 1)
    for index, surface in _author_rows(geometry["surfaces"], path + ".geometry.surfaces", 1):
        at = f"{path}.geometry.surfaces[{index}]"
        _author_fields(surface, ("id", "min_cm", "max_cm"), at)
        identifier(surface["id"], at + ".id")
        _author_vector(surface["min_cm"], at + ".min_cm")
        _author_vector(surface["max_cm"], at + ".max_cm")
    for index, blocker in _author_rows(geometry["blockers"], path + ".geometry.blockers", 10):
        at = f"{path}.geometry.blockers[{index}]"
        _author_fields(blocker, ("id", "surface_id", "polygon_cm", "height_cm", "blocks_movement", "blocks_sight"), at)
        for field in ("id", "surface_id"):
            identifier(blocker[field], at + "." + field)
        integer(blocker["height_cm"], at + ".height_cm", 1)
        for field in ("blocks_movement", "blocks_sight"):
            require(type(blocker[field]) is bool, "AuthorShape", at + "." + field, "expected a boolean flag")
        for vertex, value in _author_rows(blocker["polygon_cm"], at + ".polygon_cm", 4):
            _author_vector(value, f"{at}.polygon_cm[{vertex}]")

    placement = author["placement"]
    _author_fields(placement, ("spawn_offsets_cm", "spawn_validation_radius_cm"), path + ".placement")
    integer(placement["spawn_validation_radius_cm"], path + ".placement.spawn_validation_radius_cm", 1)
    for index, offset in _author_rows(placement["spawn_offsets_cm"], path + ".placement.spawn_offsets_cm", 7):
        _author_vector(offset, f"{path}.placement.spawn_offsets_cm[{index}]")
    for index, connection in _author_rows(author["connections"], path + ".connections", 5):
        at = f"{path}.connections[{index}]"
        _author_fields(connection, ("id", "wall_ids", "camp_id"), at)
        for field in ("id", "camp_id"):
            identifier(connection[field], at + "." + field)
        for wall, identity in _author_rows(connection["wall_ids"], at + ".wall_ids", 2):
            identifier(identity, f"{at}.wall_ids[{wall}]")
    for index, anchor in _author_rows(author["anchors"], path + ".anchors", 26):
        at = f"{path}.anchors[{index}]"
        require(type(anchor) is dict, "AuthorShape", at, "expected an anchor object")
        kind = anchor.get("kind")
        require(type(kind) is str and kind in ANCHOR_KINDS,
                "AuthorShape", at + ".kind", "unsupported anchor kind")
        fields = ("id", "kind", "surface_id", "position_cm") + (("members",) if kind == "Camp" else ())
        _author_fields(anchor, fields, at)
        identifier(anchor["id"], at + ".id")
        identifier(anchor["surface_id"], at + ".surface_id")
        _author_vector(anchor["position_cm"], at + ".position_cm")
        if kind == "Camp":
            for member, value in _author_rows(anchor["members"], at + ".members", 1, 2):
                member_at = f"{at}.members[{member}]"
                _author_fields(value, ("id", "position_cm"), member_at)
                identifier(value["id"], member_at + ".id")
                _author_vector(value["position_cm"], member_at + ".position_cm")


def project_author(author, source_raw, schema_raw):
    validate_author_contract(author)
    schema = content.AuthorSchema(content.parse_json(schema_raw, SCHEMA_SOURCE))
    schema.validate(author, path="world author")
    world = {key: content.canonical(author[key]) for key in AUTHOR_HEADER_FIELDS}
    world["source_sha256"] = content.sha256(source_raw)
    world["author_schema_sha256"] = content.sha256(schema_raw)
    geometry = author["geometry"]
    world["geometry"] = {
        "surfaces": [{"id": row["id"], "min_cm": vector(row["min_cm"]), "max_cm": vector(row["max_cm"])}
                     for row in geometry["surfaces"]],
        "blockers": [{
            "id": row["id"], "surface_id": row["surface_id"],
            "polygon_cm": [vector(vertex) for vertex in row["polygon_cm"]],
            "height_cm": pair(row["height_cm"]), "blocks_movement": row["blocks_movement"],
            "blocks_sight": row["blocks_sight"],
        } for row in geometry["blockers"]],
        "sight_sample_height_cm": pair(geometry["sight_sample_height_cm"]),
        "camp_clearance_cm": pair(geometry["camp_clearance_cm"]),
    }
    world["placement"] = {
        "spawn_offsets_cm": [vector(offset) for offset in author["placement"]["spawn_offsets_cm"]],
        "spawn_validation_radius_cm": pair(author["placement"]["spawn_validation_radius_cm"]),
    }
    world["anchors"] = []
    for row in author["anchors"]:
        anchor = {key: row[key] for key in ("id", "kind", "surface_id")}
        anchor["position_cm"] = vector(row["position_cm"])
        if row["kind"] == "Camp":
            anchor["members"] = [{"id": member["id"], "position_cm": vector(member["position_cm"])}
                                 for member in row["members"]]
        world["anchors"].append(anchor)
    for rows in (world["geometry"]["surfaces"], world["geometry"]["blockers"], world["anchors"], world["connections"]):
        rows.sort(key=lambda row: row["id"])
    return content.canonical(world)


def validate_payload(raw):
    world = content.parse_json(raw, content.WORLD_OUTPUT)
    content.validate_integer_tree(world, "world")
    keys(world, ROOT_KEYS, "world")
    integer(world["schema_version"], "world schema", SCHEMA_VERSION, SCHEMA_VERSION)
    integer(world["projection_revision"], "world projection", PROJECTION_REVISION, PROJECTION_REVISION)
    integer(world["layout_revision"], "world layout revision", 1, (1 << 32) - 1)
    integer(world["geometry_revision"], "world geometry revision", 1, (1 << 32) - 1)
    identifier(world["map_id"], "world.map_id")
    content.digest_to_wire(world["source_sha256"])
    content.digest_to_wire(world["author_schema_sha256"])
    _approval(world["approval"])
    require(type(world["parameter_refs"]) is list and tuple(world["parameter_refs"]) == PARAMETER_REFS,
            "InvalidReference", "world.parameter_refs", "expected exactly the ordinal-sorted central parameter references")
    geometry = world["geometry"]
    keys(geometry, {"surfaces", "blockers", "sight_sample_height_cm", "camp_clearance_cm"}, "world.geometry")
    require(type(geometry["surfaces"]) is list and len(geometry["surfaces"]) == 1,
            "InvalidGeometry", "world.surfaces", "exactly one flat ground surface is required")
    ids = {world["map_id"]}

    def claim(identity, path):
        identifier(identity, path)
        require(identity not in ids, "DuplicateId", path, f"duplicate world identity {identity}")
        ids.add(identity)

    surface = geometry["surfaces"][0]
    keys(surface, {"id", "min_cm", "max_cm"}, "world.surface")
    claim(surface["id"], "world.surface")
    minimum, maximum = content._world_box(surface, surface["id"])
    require(minimum[2] == maximum[2], "InvalidGeometry", surface["id"], "ground must be a single flat layer")
    floor = minimum[2]
    sight_height = content._wire_fraction(geometry["sight_sample_height_cm"], "world sight height")
    clearance = content._wire_fraction(geometry["camp_clearance_cm"], "world camp clearance")
    require(sight_height > 0 and clearance > 0, "InvalidGeometry", "world.geometry", "positive geometry dimensions are required")
    require(type(geometry["blockers"]) is list and len(geometry["blockers"]) == 10,
            "InvalidGeometry", "world.blockers", "five inner walls, four boundary walls and one stall are required")
    blockers = {}
    for row in geometry["blockers"]:
        keys(row, {"id", "surface_id", "polygon_cm", "height_cm", "blocks_movement", "blocks_sight"}, "world.blocker")
        claim(row["id"], "world.blocker")
        require(row["surface_id"] == surface["id"], "InvalidReference", row["id"], "unknown supporting surface")
        require(type(row["polygon_cm"]) is list and len(row["polygon_cm"]) == 4,
                "InvalidGeometry", row["id"], "blocker requires a convex four-vertex polygon")
        polygon = tuple(point(vertex, row["id"]) for vertex in row["polygon_cm"])
        require(all(vertex[2] == floor for vertex in polygon), "InvalidGeometry", row["id"], "blocker is on another layer")
        validate_polygon(polygon, row["id"])
        height = content._wire_fraction(row["height_cm"], row["id"])
        require(height > 0 and type(row["blocks_movement"]) is bool and row["blocks_movement"]
                and type(row["blocks_sight"]) is bool, "InvalidGeometry", row["id"], "invalid blocker flags or height")
        require(height >= sight_height if row["blocks_sight"] else height < sight_height,
                "InvalidGeometry", row["id"], "blocker height disagrees with the sight classification")
        blockers[row["id"]] = polygon
    require(sum(row["blocks_sight"] for row in geometry["blockers"]) == 9,
            "InvalidGeometry", "world.blockers", "exactly one low stall must block movement but not sight")
    placement = world["placement"]
    keys(placement, {"spawn_offsets_cm", "spawn_validation_radius_cm"}, "world.placement")
    require(type(placement["spawn_offsets_cm"]) is list and len(placement["spawn_offsets_cm"]) == 7,
            "WorldPlacement", "world.placement", "seven ordered local offsets are required")
    offsets = [point(row, "world offset") for row in placement["spawn_offsets_cm"]]
    envelope = content._wire_fraction(placement["spawn_validation_radius_cm"], "world spawn envelope")
    require(envelope > 0 and offsets[0] == (0, 0, 0) and len(set(offsets)) == 7,
            "WorldPlacement", "world.placement", "center-first unique offsets and a positive validation envelope are required")
    require(all(offset[2] == 0 and squared_distance(offset, (0, 0)) <= bounded(envelope * envelope) for offset in offsets),
            "WorldPlacement", "world.placement", "offset changes layer or exceeds its fixed validation envelope")
    require(type(world["anchors"]) is list and len(world["anchors"]) == 26,
            "WorldAnchors", "world.anchors",
            "ten spawns, five camps, one shop, one capture, two dorms, three airdrop points and four Boss waypoints are required")
    anchors = {}
    member_count = 0
    for row in world["anchors"]:
        require(type(row) is dict, "WireShape", "world.anchor", "expected object")
        fields = {"id", "kind", "surface_id", "position_cm"}
        if row.get("kind") == "Camp":
            fields.add("members")
        keys(row, fields, "world.anchor")
        claim(row["id"], "world.anchor")
        require(row["surface_id"] == surface["id"], "InvalidReference", row["id"], "unknown anchor surface")
        require(row["kind"] in ANCHOR_KINDS, "WorldAnchors", row["id"], "unknown anchor kind")
        position = point(row["position_cm"], row["id"])
        require(all(a <= p <= b for a, p, b in zip(minimum, position, maximum)),
                "InvalidGeometry", row["id"], "anchor outside the surface or off its layer")
        if row["kind"] == "Camp":
            require(type(row["members"]) is list and 1 <= len(row["members"]) <= 2,
                    "WorldAnchors", row["id"], "expected one or two member homes")
            for member in row["members"]:
                keys(member, {"id", "position_cm"}, row["id"])
                claim(member["id"], row["id"])
                home = point(member["position_cm"], member["id"])
                require(all(a <= p <= b for a, p, b in zip(minimum, home, maximum)),
                        "InvalidGeometry", member["id"], "member home outside the surface")
                member_count += 1
        anchors[row["id"]] = row
    require(member_count == 8 and {kind: sum(row["kind"] == kind for row in anchors.values())
                                  for kind in ANCHOR_KINDS}
            == {"Spawn": 10, "Camp": 5, "Shop": 1, "Capture": 1, "Dorm": 2, "Airdrop": 3, "BossWaypoint": 4},
            "WorldAnchors", "world", "incorrect anchor/member counts")
    # The native importer knows the dorms by id, so a rename must fail here rather than only at the native import.
    require({identity for identity, row in anchors.items() if row["kind"] == "Dorm"} == {"dorm_nw", "dorm_se"},
            "WorldAnchors", "world", "dorm ids must be dorm_nw and dorm_se")
    require(type(world["connections"]) is list and len(world["connections"]) == 5,
            "InvalidReference", "world.connections", "five named inner/outer connections are required")
    connected_camps, wall_degrees = set(), {}
    for row in world["connections"]:
        keys(row, {"id", "wall_ids", "camp_id"}, "world.connection")
        claim(row["id"], "world.connection")
        require(row["camp_id"] in anchors and anchors[row["camp_id"]]["kind"] == "Camp"
                and row["camp_id"] not in connected_camps, "InvalidReference", row["id"], "unknown or repeated camp connection")
        connected_camps.add(row["camp_id"])
        require(type(row["wall_ids"]) is list and len(row["wall_ids"]) == 2
                and len(set(row["wall_ids"])) == 2 and all(identity in blockers for identity in row["wall_ids"]),
                "InvalidReference", row["id"], "unknown or repeated connection wall")
        for identity in row["wall_ids"]:
            wall_degrees[identity] = wall_degrees.get(identity, 0) + 1
    require(len(wall_degrees) == 5 and set(wall_degrees.values()) == {2},
            "InvalidReference", "world.connections", "connections must form the five-wall ring")
    for rows in (geometry["surfaces"], geometry["blockers"], world["anchors"], world["connections"]):
        require([row["id"] for row in rows] == sorted(row["id"] for row in rows),
                "NonCanonical", "world", "definition rows must be ordinal-sorted by ID")
    require(raw == encode(content.canonical(world)), "NonCanonical", content.WORLD_OUTPUT, "expected canonical integer bytes")
    return world, anchors


class Geometry:
    def __init__(self, world):
        surface = world["geometry"]["surfaces"][0]
        self.minimum = point(surface["min_cm"], surface["id"])
        self.maximum = point(surface["max_cm"], surface["id"])
        self.polygons = {row["id"]: tuple(point(p, row["id"]) for p in row["polygon_cm"])
                         for row in world["geometry"]["blockers"]}
        self.rows = {row["id"]: row for row in world["geometry"]["blockers"]}

    def legal(self, p, radius=Fraction(0)):
        return (p[2] == self.minimum[2]
                and all(self.minimum[i] + radius <= p[i] <= self.maximum[i] - radius for i in (0, 1))
                and all(not inside(p, polygon, strict=True)
                        and polygon_distance_squared(p, polygon) >= bounded(radius * radius)
                        for polygon in self.polygons.values()))

    def clearance(self, p):
        distances = [p[i] - self.minimum[i] for i in (0, 1)] + [self.maximum[i] - p[i] for i in (0, 1)]
        return min(*(float(value) for value in distances),
                   *(math.sqrt(float(polygon_distance_squared(p, polygon))) for polygon in self.polygons.values()))

    def sight_clear(self, start, end):
        require(start[2] == end[2] == self.minimum[2], "InvalidGeometry", "sight",
                "the single-layer model requires grounded foot samples")
        # Strict interior intersection; a tangent along a wall boundary is not an interior crossing.
        for identity, polygon in self.polygons.items():
            if not self.rows[identity]["blocks_sight"]:
                continue
            lower, upper = Fraction(0), Fraction(1)
            for a, b in edges(polygon):
                delta = sub(b, a)
                constant, slope = cross(delta, sub(start, a)), cross(delta, sub(end, start))
                if slope == 0:
                    if constant <= 0:
                        break
                elif slope > 0:
                    lower = max(lower, bounded(-constant / slope))
                else:
                    upper = min(upper, bounded(-constant / slope))
            else:
                if lower < upper and upper > 0 and lower < 1:
                    return False
        return True


class PathModel:
    """Conservative static miter/visibility-graph model, never a runtime movement provider."""
    EPS = 1e-7

    def __init__(self, geometry, radius):
        self.bounds = (float(geometry.minimum[0] + radius), float(geometry.minimum[1] + radius),
                       float(geometry.maximum[0] - radius), float(geometry.maximum[1] - radius))
        self.polygons, self.planes = [], []
        self.by_id = {}
        for identity, polygon in geometry.polygons.items():
            lines = []
            for a, b in edges(polygon):
                dx, dy = float(b[0] - a[0]), float(b[1] - a[1])
                length = math.hypot(dx, dy)
                nx, ny = -dy / length, dx / length
                lines.append((nx, ny, nx * float(a[0]) + ny * float(a[1]) - float(radius)))
            expanded = []
            for index, (nx, ny, c) in enumerate(lines):
                px, py, d = lines[index - 1]
                determinant = px * ny - py * nx
                require(abs(determinant) > 1e-12, "InvalidGeometry", identity, "degenerate miter corner")
                expanded.append(((d * ny - py * c) / determinant, (px * c - d * nx) / determinant))
            self.by_id[identity] = expanded
            if (max(p[0] for p in expanded) <= self.bounds[0] + self.EPS
                    or min(p[0] for p in expanded) >= self.bounds[2] - self.EPS
                    or max(p[1] for p in expanded) <= self.bounds[1] + self.EPS
                    or min(p[1] for p in expanded) >= self.bounds[3] - self.EPS):
                continue
            self.polygons.append(expanded)
            self.planes.append(lines)
        self.nodes = [p for polygon in self.polygons for p in polygon if self.legal(p)]
        self.graph = [[] for _ in self.nodes]
        for i, a in enumerate(self.nodes):
            for j in range(i):
                b = self.nodes[j]
                if self.clear(a, b):
                    distance = math.dist(a, b)
                    self.graph[i].append((j, distance))
                    self.graph[j].append((i, distance))
        self.goals = {}

    def legal(self, p):
        return (self.bounds[0] - self.EPS <= p[0] <= self.bounds[2] + self.EPS
                and self.bounds[1] - self.EPS <= p[1] <= self.bounds[3] + self.EPS
                and not any(all(nx * p[0] + ny * p[1] - c > self.EPS for nx, ny, c in lines)
                            for lines in self.planes))

    def clear(self, a, b):
        if not self.legal(a) or not self.legal(b):
            return False
        for lines in self.planes:
            lower, upper = 0.0, 1.0
            for nx, ny, c in lines:
                constant = nx * a[0] + ny * a[1] - c
                slope = nx * (b[0] - a[0]) + ny * (b[1] - a[1])
                if abs(slope) < self.EPS:
                    if constant <= self.EPS:
                        break
                elif slope > 0:
                    lower = max(lower, (self.EPS - constant) / slope)
                else:
                    upper = min(upper, (self.EPS - constant) / slope)
            else:
                if lower < upper - 1e-12 and upper > 0 and lower < 1:
                    return False
        return True

    def direct_to_circle(self, start, center, radius):
        distance = math.dist(start, center)
        if distance <= radius:
            return 0.0 if self.legal(start) else math.inf
        end = tuple(center[i] + (start[i] - center[i]) * radius / distance for i in (0, 1))
        return distance - radius if self.clear(start, end) else math.inf

    def distance(self, start, center, radius):
        start, center, radius = tuple(map(float, start[:2])), tuple(map(float, center[:2])), float(radius)
        require(self.legal(start), "WorldPath", "start", "illegal static capsule position")
        key = (*center, radius)
        if key not in self.goals:
            distances = [self.direct_to_circle(node, center, radius) for node in self.nodes]
            queue = [(value, i) for i, value in enumerate(distances) if math.isfinite(value)]
            heapq.heapify(queue)
            while queue:
                distance, i = heapq.heappop(queue)
                if distance != distances[i]:
                    continue
                for j, length in self.graph[i]:
                    candidate = distance + length
                    if candidate < distances[j]:
                        distances[j] = candidate
                        heapq.heappush(queue, (candidate, j))
            self.goals[key] = distances
        result = self.direct_to_circle(start, center, radius)
        for node, remaining in zip(self.nodes, self.goals[key]):
            if math.isfinite(remaining) and self.clear(start, node):
                result = min(result, math.dist(start, node) + remaining)
        require(math.isfinite(result), "WorldPath", "goal", "no complete static path to a legal destination boundary")
        return result


def validate_bindings(world, data):
    anchors = content.rows_by_id(world["anchors"], "world anchors")
    content.validate_world_references(data, world, anchors)
    for key in PARAMETER_REFS:
        require(key in data["parameters"], "InvalidReference", key, "unresolved central parameter")
    p = lambda key: content.quantity(data["parameters"][key])
    radius = p("UNIT.CollisionRadiusCm")
    geometry = Geometry(world)
    model = PathModel(geometry, radius)
    capture = point(anchors[data["world"]["capture_id"]]["position_cm"], "capture")
    shop = point(anchors[data["world"]["shop_id"]]["position_cm"], "shop")
    capture_radius, shop_radius = p("CAPTURE.RadiusCm"), p("SHOP.InteractRadiusCm")
    require(geometry.legal(capture, radius + capture_radius), "InvalidGeometry", "capture", "capture disk lacks legal capsule support")
    require(squared_distance(capture, shop) > bounded((capture_radius + shop_radius) ** 2),
            "InvalidGeometry", "shop", "shop eligibility and capture disks overlap")
    # D21 dorms and D26 airdrop points: every point of the disk is legal standing ground, and the disk stays apart
    # from the circle, the shop, every camp clearing and every other event disk, so no two zone rules overlap.
    camp_clearance = content._wire_fraction(world["geometry"]["camp_clearance_cm"], "camp clearance")
    # D26 Boss waypoints are canonical boss_patrol_1..4 on legal standing ground for one capsule; the path between
    # them is the runtime's legal navigation.
    waypoints = sorted(identity for identity, row in anchors.items() if row["kind"] == "BossWaypoint")
    require(waypoints == [f"boss_patrol_{n}" for n in range(1, 5)], "WorldAnchors", "boss_patrol",
            "the Boss waypoints must be boss_patrol_1 to boss_patrol_4")
    for identity in waypoints:
        require(geometry.legal(point(anchors[identity]["position_cm"], identity), radius), "InvalidGeometry", identity,
                "Boss waypoint lacks legal capsule support")
    zone_radius = {"Dorm": p("DORM.RadiusCm"), "Airdrop": p("AIRDROP.PickupRadiusCm")}
    zones = [(identity, row["kind"].lower(), point(row["position_cm"], identity), zone_radius[row["kind"]])
             for identity, row in anchors.items() if row["kind"] in zone_radius]
    for identity, label, centre, zone in zones:
        require(zone > 0 and geometry.legal(centre, radius + zone), "InvalidGeometry", identity,
                f"{label} disk lacks legal capsule support")
        for other, other_radius in ((capture, capture_radius), (shop, shop_radius)):
            require(squared_distance(centre, other) > bounded((zone + other_radius) ** 2),
                    "InvalidGeometry", identity, f"{label} disk overlaps the capture or shop disk")
        for camp_id, camp_row in anchors.items():
            if camp_row["kind"] == "Camp":
                require(squared_distance(centre, point(camp_row["position_cm"], camp_id))
                        > bounded((zone + camp_clearance) ** 2), "InvalidGeometry", identity,
                        f"{label} disk overlaps a camp clearing")
        for other_id, _, other_centre, other_zone in zones:
            if other_id != identity:
                require(squared_distance(centre, other_centre) > bounded((zone + other_zone) ** 2),
                        "InvalidGeometry", identity, f"{label} disk overlaps another event disk")
    stalls = [identity for identity, row in geometry.rows.items() if not row["blocks_sight"]]
    require(len(stalls) == 1 and inside(shop, geometry.polygons[stalls[0]]),
            "InvalidGeometry", "shop", "shop anchor must be in the single non-sight-blocking stall")
    # The approved goal disks do not cut through an unrelated obstacle. A disk may
    # contain the shop stall; that interior is never used as a movement endpoint.
    for identity, polygon in geometry.polygons.items():
        require(all(squared_distance(vertex, shop) < shop_radius ** 2 for vertex in polygon)
                or polygon_distance_squared(shop, polygon) > (shop_radius + radius) ** 2,
                "InvalidGeometry", identity, "shop boundary crosses a blocker")
    offsets = [point(value, "spawn offset") for value in world["placement"]["spawn_offsets_cm"]]
    for i, offset in enumerate(offsets):
        for other in offsets[:i]:
            require(squared_distance(offset, other) >= bounded((2 * radius) ** 2),
                    "WorldPlacement", "spawn offsets", "local placements overlap at the current capsule radius")
    clear_radius = content._wire_fraction(world["geometry"]["camp_clearance_cm"], "camp clearance")
    reset_radius = p("CAMP.ResetArrivalRadiusCm")
    monsters = content.rows_by_id(data["camps"]["monsters"], "monsters")
    placements = {row["anchor_id"]: monsters[row["monster_id"]] for row in data["camps"]["placements"]}
    starts, candidates, home_clearance = {}, {}, {}
    for identity, row in anchors.items():
        location = point(row["position_cm"], identity)
        if row["kind"] == "Shop":
            continue
        require(geometry.legal(location, radius), "InvalidGeometry", identity, "anchor has no legal static capsule position")
        starts[identity] = location
        if row["kind"] == "Spawn":
            for index, offset in enumerate(offsets):
                candidate = tuple(bounded(a + b) for a, b in zip(location, offset))
                require(geometry.legal(candidate, radius), "WorldPlacement", identity, "local spawn candidate is blocked")
                candidates[f"{identity}:{index}"] = candidate
        if row["kind"] == "Camp":
            require(geometry.legal(location, clear_radius), "InvalidGeometry", identity, "camp clearing is obstructed")
            monster_radius = content.quantity(placements[identity]["collision_radius"])
            require(p("UNIT.BasicAttackRangeCm") >= radius + monster_radius,
                    "InvalidGeometry", identity, "base attack requires overlapping actor and monster capsules")
            homes = [point(member["position_cm"], member["id"]) for member in row["members"]]
            for index, member in enumerate(row["members"]):
                home = homes[index]
                require(geometry.legal(home, monster_radius + reset_radius), "InvalidGeometry", member["id"],
                        "member reset region lacks static capsule support")
                require(squared_distance(home, location) <= clear_radius ** 2,
                        "InvalidGeometry", member["id"], "member home is outside its clearing")
                for other in homes[:index]:
                    require(squared_distance(home, other) >= (2 * monster_radius) ** 2,
                            "InvalidGeometry", member["id"], "member homes overlap")
                home_clearance[member["id"]] = geometry.clearance(home)
                starts[member["id"]] = home
    connection_walls = set()
    for connection in world["connections"]:
        first, second = (geometry.polygons[identity] for identity in connection["wall_ids"])
        centers = [tuple(sum(vertex[i] for vertex in polygon) / 4 for i in range(3)) for polygon in (first, second)]
        camp = point(anchors[connection["camp_id"]]["position_cm"], connection["camp_id"])
        direction = sub(camp, capture)
        require(cross(direction, sub(centers[0], capture)) < 0 < cross(direction, sub(centers[1], capture)),
                "InvalidGeometry", connection["id"], "walls do not bound the declared camp-facing opening")
        require(polygon_gap_squared(first, second) > (2 * radius) ** 2,
                "WorldPath", connection["id"], "opening is too narrow for the shared capsule")
        require(model.direct_to_circle(tuple(map(float, camp[:2])), tuple(map(float, capture[:2])),
                                       float(capture_radius)) < math.inf,
                "WorldPath", connection["id"], "declared inner/outer connection is blocked")
        connection_walls.update(connection["wall_ids"])
    min_x, min_y, _ = geometry.minimum
    max_x, max_y, _ = geometry.maximum
    sides = ((((min_x, min_y), (max_x, min_y)), 1, min_y, -1),
             (((max_x, min_y), (max_x, max_y)), 0, max_x, 1),
             (((max_x, max_y), (min_x, max_y)), 1, max_y, 1),
             (((min_x, max_y), (min_x, min_y)), 0, min_x, -1))
    boundary_walls = set()
    for side, axis, coordinate, direction in sides:
        matches = [identity for identity, polygon in geometry.polygons.items()
                   if geometry.rows[identity]["blocks_sight"]
                   and all(inside(vertex, polygon) for vertex in side)
                   and all((vertex[axis] - coordinate) * direction >= 0 for vertex in polygon)]
        require(len(matches) == 1,
                "InvalidGeometry", "boundary", "surface boundary lacks a complete physical wall")
        boundary_walls.update(matches)
    require(len(boundary_walls) == 4 and not boundary_walls.intersection(connection_walls)
            and set(geometry.polygons) == boundary_walls | connection_walls | set(stalls),
            "InvalidGeometry", "world", "blockers are not exactly the boundary, five connected inner walls and stall")
    inner_polygons = [model.by_id[identity] for identity in sorted(connection_walls | set(stalls))]
    for index, polygon in enumerate(inner_polygons):
        require(all(model.bounds[0] < vertex[0] < model.bounds[2]
                    and model.bounds[1] < vertex[1] < model.bounds[3] for vertex in polygon),
                "WorldPath", "route bound", "expanded inner blocker reaches the world boundary")
        for other in inner_polygons[:index]:
            separated = False
            for source, target in ((polygon, other), (other, polygon)):
                for a, b in edges(source):
                    dx, dy = b[0] - a[0], b[1] - a[1]
                    if all(dx * (p[1] - a[1]) - dy * (p[0] - a[0]) < -PathModel.EPS for p in target):
                        separated = True
                        break
                if separated:
                    break
            require(separated, "WorldPath", "route bound", "expanded inner blockers overlap or touch")
    intervals = []
    tau = 2 * math.pi
    for identity in connection_walls:
        polygon = model.by_id[identity]
        angles = sorted(math.atan2(vertex[1] - float(capture[1]), vertex[0] - float(capture[0])) % tau for vertex in polygon)
        gaps = [((angles[(i + 1) % 4] - angles[i]) % tau, i) for i in range(4)]
        _, index = max(gaps)
        low = angles[(index + 1) % 4]
        high = angles[index] + (tau if angles[index] < low else 0)
        require(high - low < math.pi, "WorldPath", identity, "inner wall spans the capture origin")
        intervals.append((low, high))
    intervals.sort()
    angular_gaps = [intervals[(i + 1) % 5][0] + (tau if i == 4 else 0) - intervals[i][1] for i in range(5)]
    require(min(angular_gaps) > 0, "WorldPath", "radial bound",
            "inner-wall angular intervals overlap; the one-inner-wall route bound is not valid")
    routes = {identity: {"capture_cm": model.distance(location, capture, capture_radius),
                         "shop_cm": model.distance(location, shop, shop_radius)}
              for identity, location in {**starts, **candidates}.items()}
    budget = float(p("UNIT.MoveSpeedCmPerSec") * p("TIMING.DefendRouteBudgetSec"))
    require(all(row["capture_cm"] <= budget for row in routes.values()),
            "WorldPath", "defend budget", "a required point exceeds the central base-speed route budget")
    inner_half_perimeters = [
        sum(math.dist(a, b) for a, b in edges(model.by_id[identity])) / 2 for identity in connection_walls
    ]
    stall_half_perimeter = sum(math.dist(a, b) for a, b in edges(model.by_id[stalls[0]])) / 2
    farthest = max(math.dist((x, y), tuple(map(float, capture[:2])))
                   for x in (model.bounds[0], model.bounds[2]) for y in (model.bounds[1], model.bounds[3]))
    route_upper = farthest - float(capture_radius) + max(inner_half_perimeters) + stall_half_perimeter
    require(route_upper <= budget, "WorldPath", "defend budget", "conservative whole-ground route bound exceeds the budget")
    return {"routes": routes, "home_clearance_cm": home_clearance, "route_upper_bound_cm": route_upper,
            "min_inner_angular_gap_degrees": math.degrees(min(angular_gaps)),
            "spawn_candidates": len(candidates), "member_homes": len(home_clearance)}


@dataclass(frozen=True)
class WorldCompilation:
    world: Mapping[str, object]
    report: Mapping[str, object]
    outputs: Mapping[Path, bytes]
    inputs: Mapping[Path, bytes]
    digest: bytes
    bundle_kind: str = "world"


def world_contract_bytes(payload):
    return (
        "#pragma once\n"
        f"#define LD_DEMO_WORLD_SCHEMA_VERSION {SCHEMA_VERSION}\n"
        f"#define LD_DEMO_WORLD_PROJECTION_REVISION {PROJECTION_REVISION}\n"
        f'#define LD_DEMO_WORLD_PAYLOAD_SHA256 TEXT("{content.sha256(payload)}")\n'
    ).encode("ascii")


# The only catalog sections validate_bindings may read. compile_world passes it exactly these, so reading any other
# section fails instead of silently escaping the reuse key below.
BINDING_SECTIONS = ("parameters", "world", "camps")
_reuse = None


@contextmanager
def reuse_binding_reports():
    """Within the block, reuse the binding report for an identical world payload and identical binding sections.

    validate_bindings is the expensive, pure part of compile_world. Its key is the world payload bytes plus the
    canonical BINDING_SECTIONS it is given, so a reused report is the one recomputing would produce. Test fixture
    export uses this; nothing else does, and the cache is dropped when the block ends.
    """
    global _reuse
    require(_reuse is None, "ReuseActive", SOURCE, "world binding reuse cannot be nested")
    _reuse = {}
    try:
        yield
    finally:
        _reuse = None


def _binding_report(world, raw, data):
    view = MappingProxyType({section: data[section] for section in BINDING_SECTIONS})
    if _reuse is None:
        return content.freeze(validate_bindings(world, view))
    key = (raw, encode(content.canonical(view)))
    if key not in _reuse:
        _reuse[key] = content.freeze(validate_bindings(world, view))
    return _reuse[key]


def compile_world(root=content.ROOT, author=None):
    root = Path(root)
    author = content.compile_author(root) if author is None else author
    inputs = dict(author.inputs)
    inputs[SOURCE] = content.read_bytes(root, SOURCE)
    inputs[SCHEMA_SOURCE] = content.read_bytes(root, SCHEMA_SOURCE)
    source = content.parse_json(inputs[SOURCE], SOURCE)
    world = project_author(source, inputs[SOURCE], inputs[SCHEMA_SOURCE])
    raw = encode(world)
    validate_payload(raw)
    report = _binding_report(world, raw, author.catalog.data)
    outputs = {content.WORLD_OUTPUT: raw, content.WORLD_CONTRACT: world_contract_bytes(raw)}
    for relative, original in inputs.items():
        require(content.read_bytes(root, relative) == original, "InputsChanged", relative, "world input changed during compilation")
    return WorldCompilation(content.freeze(world), report, MappingProxyType(outputs),
                            MappingProxyType(inputs), content.digest_to_wire(content.sha256(raw)))
