"""Single approved-world producer and closed blocker contract; no runtime navigation state."""
from collections.abc import Mapping
from contextlib import contextmanager
from dataclasses import dataclass
from fractions import Fraction
import math
from pathlib import Path
from types import MappingProxyType

from CatalogTools import encode
import DemoContent as content


SCHEMA_VERSION = 3
PROJECTION_REVISION = 3
SOURCE = content.WORLD_SOURCE
SCHEMA_SOURCE = content.WORLD_SCHEMA_SOURCE
OUTPUT_PATHS = (content.WORLD_OUTPUT, content.WORLD_CONTRACT)
PARAMETER_REFS = (
    "AIRDROP.PickupRadiusCm", "CAMP.ResetArrivalRadiusCm", "CAPTURE.RadiusCm", "DORM.RadiusCm", "SHOP.InteractRadiusCm",
    "UNIT.BasicAttackRangeCm", "UNIT.CollisionRadiusCm", "VISION.RadiusCm",
)
# D21 (#79): a Dorm is a public recovery disk of DORM.RadiusCm; D26 (#79): an Airdrop is a public drop point whose
# pickup disk is AIRDROP.PickupRadiusCm. Neither adds collision, navigation or sight blocking.
# D26 (#79): a BossWaypoint is one of the four public points the 画饼老板 Boss patrols in id order; a point only.
ANCHOR_KINDS = ("Spawn", "Camp", "Shop", "Capture", "Dorm", "Airdrop", "BossWaypoint")
# #97: anchor counts follow the authored world within these bounds. At least one spawn per match seat; one capture;
# the Boss patrols exactly four waypoints.
MAX_ANCHORS = 128
# #97: forests of body-sized trunks need well over a thousand blockers.
MAX_BLOCKERS = 2048
ANCHOR_COUNTS = {"Spawn": (6, 32), "Camp": (1, 48), "Shop": (1, 8), "Capture": (1, 1), "Dorm": (1, 8),
                 "Airdrop": (1, 16), "BossWaypoint": (4, 4)}
# A blocker is a convex polygon prism (walls, stalls) or an upright cylinder (trees); each carries its own height and
# movement/sight classification.
BLOCKER_SHAPES = ("polygon", "cylinder")
POLYGON_VERTICES = (3, 16)
# #97 elevation: flat levels 0 (the ground) to MAX_LEVEL, level_step_cm apart. A plateau raises a convex footprint to its
# level; a ramp joins a level to the next. Plateau edges no ramp meets are cliffs: they stop movement, and only the
# higher side sees across them. A ramp is at most RAMP_SLOPE steep (rise / run), under the bodies' 40 degree floor.
MAX_LEVEL = 2
RAMP_SLOPE = Fraction(7, 10)
ELEVATION_ROWS = 64
ROOT_KEYS = {
    "schema_version", "projection_revision", "map_id", "layout_revision", "geometry_revision",
    "source_sha256", "author_schema_sha256", "approval", "geometry", "placement",
    "parameter_refs", "anchors",
}
AUTHOR_HEADER_FIELDS = (
    "schema_version", "projection_revision", "map_id", "layout_revision",
    "geometry_revision", "approval", "parameter_refs",
)
BLOCKER_FIELDS = {
    "polygon": ("id", "surface_id", "shape", "polygon_cm", "height_cm", "blocks_movement", "blocks_sight"),
    "cylinder": ("id", "surface_id", "shape", "center_cm", "radius_cm", "height_cm", "blocks_movement", "blocks_sight"),
}
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


def validate_polygon(polygon, path):
    count = len(polygon)
    require(POLYGON_VERTICES[0] <= count <= POLYGON_VERTICES[1] and len(set(polygon)) == count,
            "InvalidGeometry", path, f"{POLYGON_VERTICES[0]}..{POLYGON_VERTICES[1]} distinct polygon vertices are required")
    require(all(cross(sub(polygon[(i + 1) % count], polygon[i]),
                      sub(polygon[(i + 2) % count], polygon[(i + 1) % count])) > 0 for i in range(count)),
            "InvalidGeometry", path, "polygon must be strictly convex, simple and counterclockwise")
    # Every vertex turning left still admits a star that winds twice; a convex polygon's edge directions only advance.
    # Each edge must keep the whole polygon on its left (or on its line), which only a once-winding polygon does.
    require(all(cross(sub(b, a), sub(p, a)) >= 0 for a, b in edges(polygon) for p in polygon),
            "InvalidGeometry", path, "polygon must wind exactly once")
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
    _author_fields(geometry, ("surfaces", "elevation", "blockers", "sight_sample_height_cm", "camp_clearance_cm"),
                   path + ".geometry")
    elevation = geometry["elevation"]
    _author_fields(elevation, ("level_step_cm", "plateaus", "ramps"), path + ".geometry.elevation")
    integer(elevation["level_step_cm"], path + ".geometry.elevation.level_step_cm", 1)
    for kind, level_key, count in (("plateaus", "level", (3, 16)), ("ramps", "lower_level", (4, 4))):
        for index, row in _author_rows(elevation[kind], f"{path}.geometry.elevation.{kind}", 0, ELEVATION_ROWS):
            at = f"{path}.geometry.elevation.{kind}[{index}]"
            _author_fields(row, ("id", level_key, "polygon_cm"), at)
            identifier(row["id"], at + ".id")
            integer(row[level_key], f"{at}.{level_key}", 0, MAX_LEVEL)
            for vertex, value in _author_rows(row["polygon_cm"], at + ".polygon_cm", *count):
                _author_vector(value, f"{at}.polygon_cm[{vertex}]")
    for field in ("sight_sample_height_cm", "camp_clearance_cm"):
        integer(geometry[field], f"{path}.geometry.{field}", 1)
    for index, surface in _author_rows(geometry["surfaces"], path + ".geometry.surfaces", 1):
        at = f"{path}.geometry.surfaces[{index}]"
        _author_fields(surface, ("id", "min_cm", "max_cm"), at)
        identifier(surface["id"], at + ".id")
        _author_vector(surface["min_cm"], at + ".min_cm")
        _author_vector(surface["max_cm"], at + ".max_cm")
    for index, blocker in _author_rows(geometry["blockers"], path + ".geometry.blockers", 4, MAX_BLOCKERS):
        at = f"{path}.geometry.blockers[{index}]"
        require(type(blocker) is dict and blocker.get("shape") in BLOCKER_SHAPES,
                "AuthorShape", at + ".shape", "unsupported blocker shape")
        _author_fields(blocker, BLOCKER_FIELDS[blocker["shape"]], at)
        for field in ("id", "surface_id"):
            identifier(blocker[field], at + "." + field)
        integer(blocker["height_cm"], at + ".height_cm", 1)
        for field in ("blocks_movement", "blocks_sight"):
            require(type(blocker[field]) is bool, "AuthorShape", at + "." + field, "expected a boolean flag")
        if blocker["shape"] == "polygon":
            for vertex, value in _author_rows(blocker["polygon_cm"], at + ".polygon_cm", *POLYGON_VERTICES):
                _author_vector(value, f"{at}.polygon_cm[{vertex}]")
        else:
            _author_vector(blocker["center_cm"], at + ".center_cm")
            integer(blocker["radius_cm"], at + ".radius_cm", 1)

    placement = author["placement"]
    _author_fields(placement, ("spawn_offsets_cm", "spawn_validation_radius_cm"), path + ".placement")
    integer(placement["spawn_validation_radius_cm"], path + ".placement.spawn_validation_radius_cm", 1)
    for index, offset in _author_rows(placement["spawn_offsets_cm"], path + ".placement.spawn_offsets_cm", 7):
        _author_vector(offset, f"{path}.placement.spawn_offsets_cm[{index}]")
    for index, anchor in _author_rows(author["anchors"], path + ".anchors", 1, MAX_ANCHORS):
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
        "elevation": {
            "level_step_cm": pair(geometry["elevation"]["level_step_cm"]),
            "plateaus": sorted(({"id": row["id"], "level": row["level"],
                                 "polygon_cm": [vector(vertex) for vertex in row["polygon_cm"]]}
                                for row in geometry["elevation"]["plateaus"]), key=lambda row: row["id"]),
            "ramps": sorted(({"id": row["id"], "lower_level": row["lower_level"],
                              "polygon_cm": [vector(vertex) for vertex in row["polygon_cm"]]}
                             for row in geometry["elevation"]["ramps"]), key=lambda row: row["id"]),
        },
        "blockers": [_project_blocker(row) for row in geometry["blockers"]],
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
    for rows in (world["geometry"]["surfaces"], world["geometry"]["blockers"], world["anchors"]):
        rows.sort(key=lambda row: row["id"])
    return content.canonical(world)


def _project_blocker(row):
    blocker = {"id": row["id"], "surface_id": row["surface_id"], "shape": row["shape"]}
    if row["shape"] == "polygon":
        blocker["polygon_cm"] = [vector(vertex) for vertex in row["polygon_cm"]]
    else:
        blocker["center_cm"] = vector(row["center_cm"])
        blocker["radius_cm"] = pair(row["radius_cm"])
    blocker.update(height_cm=pair(row["height_cm"]), blocks_movement=row["blocks_movement"],
                   blocks_sight=row["blocks_sight"])
    return blocker


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
    keys(geometry, {"surfaces", "elevation", "blockers", "sight_sample_height_cm", "camp_clearance_cm"}, "world.geometry")
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
    require(minimum[2] == maximum[2] == 0, "InvalidGeometry", surface["id"], "the ground is the flat base level at Z=0")
    terrain = Terrain(geometry["elevation"], minimum, maximum, claim)
    sight_height = content._wire_fraction(geometry["sight_sample_height_cm"], "world sight height")
    clearance = content._wire_fraction(geometry["camp_clearance_cm"], "world camp clearance")
    require(sight_height > 0 and clearance > 0, "InvalidGeometry", "world.geometry", "positive geometry dimensions are required")
    require(type(geometry["blockers"]) is list and 4 <= len(geometry["blockers"]) <= MAX_BLOCKERS,
            "InvalidGeometry", "world.blockers", f"four boundary walls and at most {MAX_BLOCKERS} blockers in total are required")
    for row in geometry["blockers"]:
        require(type(row) is dict and row.get("shape") in BLOCKER_SHAPES,
                "WireShape", "world.blocker", "unknown blocker shape")
        keys(row, set(BLOCKER_FIELDS[row["shape"]]), "world.blocker")
        claim(row["id"], "world.blocker")
        require(row["surface_id"] == surface["id"], "InvalidReference", row["id"], "unknown supporting surface")
        if row["shape"] == "polygon":
            require(type(row["polygon_cm"]) is list
                    and POLYGON_VERTICES[0] <= len(row["polygon_cm"]) <= POLYGON_VERTICES[1],
                    "InvalidGeometry", row["id"], "blocker requires a convex polygon of 3..16 vertices")
            polygon = tuple(point(vertex, row["id"]) for vertex in row["polygon_cm"])
            validate_polygon(polygon, row["id"])
            level = terrain.ring_level(polygon)
            require(level is not None and all(vertex[2] == terrain.height(level) for vertex in polygon),
                    "InvalidGeometry", row["id"], "blocker must stand wholly on one flat level at its height")
        else:
            centre = point(row["center_cm"], row["id"])
            size = content._wire_fraction(row["radius_cm"], row["id"])
            require(size > 0, "InvalidGeometry", row["id"], "cylinder requires a positive radius")
            level = terrain.disc_level(centre, size)
            require(level is not None and centre[2] == terrain.height(level),
                    "InvalidGeometry", row["id"], "blocker must stand wholly on one flat level at its height")
        height = content._wire_fraction(row["height_cm"], row["id"])
        require(height > 0 and type(row["blocks_movement"]) is bool and row["blocks_movement"]
                and type(row["blocks_sight"]) is bool, "InvalidGeometry", row["id"], "invalid blocker flags or height")
        require(height > sight_height if row["blocks_sight"] else height < sight_height,
                "InvalidGeometry", row["id"], "blocker height disagrees with the sight classification")
        if row["shape"] == "polygon" and not any(all(minimum[i] < vertex[i] < maximum[i] for i in (0, 1)) for vertex in polygon):
            require(height > terrain.height(terrain.highest) + sight_height, "InvalidGeometry", row["id"],
                    "boundary wall must stand taller than the highest ground plus the sight sample")
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
    require(type(world["anchors"]) is list and 1 <= len(world["anchors"]) <= MAX_ANCHORS,
            "WorldAnchors", "world.anchors", f"expected 1..{MAX_ANCHORS} anchors")
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
        level = terrain.disc_level(position, Fraction(0))
        require(all(minimum[i] <= position[i] <= maximum[i] for i in (0, 1)) and level is not None
                and position[2] == terrain.height(level),
                "InvalidGeometry", row["id"], "anchor outside the surface or off its level's height")
        if row["kind"] == "Camp":
            require(type(row["members"]) is list and 1 <= len(row["members"]) <= 2,
                    "WorldAnchors", row["id"], "expected one or two member homes")
            for member in row["members"]:
                keys(member, {"id", "position_cm"}, row["id"])
                claim(member["id"], row["id"])
                home = point(member["position_cm"], member["id"])
                level = terrain.disc_level(home, Fraction(0))
                require(all(minimum[i] <= home[i] <= maximum[i] for i in (0, 1)) and level is not None
                        and home[2] == terrain.height(level),
                        "InvalidGeometry", member["id"], "member home outside the surface or off its level's height")
                member_count += 1
        anchors[row["id"]] = row
    for kind, (low, high) in ANCHOR_COUNTS.items():
        count = sum(row["kind"] == kind for row in anchors.values())
        require(low <= count <= high, "WorldAnchors", "world", f"expected {low}..{high} {kind} anchors, found {count}")
    for rows in (geometry["surfaces"], geometry["blockers"], world["anchors"]):
        require([row["id"] for row in rows] == sorted(row["id"] for row in rows),
                "NonCanonical", "world", "definition rows must be ordinal-sorted by ID")
    require(raw == encode(content.canonical(world)), "NonCanonical", content.WORLD_OUTPUT, "expected canonical integer bytes")
    return world, anchors


def disc_inside(polygon, centre, radius):
    """The closed disc lies in the closed convex polygon."""
    inner = min(float_offsets(polygon, centre))
    if inner >= float(radius) + SCREEN_CM or inner < float(radius) - SCREEN_CM:
        return inner >= float(radius)
    return all(cross(sub(b, a), sub(centre, a)) >= 0
               and bounded(cross(sub(b, a), sub(centre, a)) ** 2) >= bounded(radius * radius * dot(sub(b, a), sub(b, a)))
               for a, b in edges(polygon))


def disc_overlaps(polygon, centre, radius):
    """The open disc meets the open interior of the convex polygon."""
    inner, gap = min(float_offsets(polygon, centre)), float_boundary_distance(polygon, centre)
    if inner > SCREEN_CM or gap < float(radius) - SCREEN_CM:
        return True
    if inner < -SCREEN_CM and gap > float(radius) + SCREEN_CM:
        return False
    return inside(centre, polygon, strict=True) or any(
        segment_distance_squared(centre, a, b) < bounded(radius * radius) for a, b in edges(polygon))


# #97 forests hold over a thousand trunks and every exact step here is a Fraction. Like the native importer, a screen skips
# an exact contact test only when plain floats put the shapes' bounds apart by more than SCREEN_CM, far beyond float error
# at map scale; anything nearer still runs exactly, so no verdict changes.
SCREEN_CM = 1.0


_FLOAT_RINGS = {}


def float_ring(polygon):
    ring = _FLOAT_RINGS.get(polygon)
    if ring is None:
        ring = _FLOAT_RINGS[polygon] = tuple((float(vertex[0]), float(vertex[1])) for vertex in polygon)
    return ring


def float_offsets(polygon, centre):
    """Float signed distances of centre inside each edge line of the counter-clockwise convex polygon (inside > 0)."""
    x, y, ring = float(centre[0]), float(centre[1]), float_ring(polygon)
    offsets = []
    for (ax, ay), (bx, by) in zip(ring, ring[1:] + ring[:1]):
        dx, dy = bx - ax, by - ay
        offsets.append((dx * (y - ay) - dy * (x - ax)) / math.hypot(dx, dy))
    return offsets


def float_boundary_distance(polygon, centre):
    """Float distance from centre to the polygon's boundary."""
    x, y, ring = float(centre[0]), float(centre[1]), float_ring(polygon)
    best = math.inf
    for (ax, ay), (bx, by) in zip(ring, ring[1:] + ring[:1]):
        dx, dy = bx - ax, by - ay
        t = min(1.0, max(0.0, ((x - ax) * dx + (y - ay) * dy) / (dx * dx + dy * dy)))
        best = min(best, math.hypot(x - ax - t * dx, y - ay - t * dy))
    return best


def float_polygon_distance(polygon, centre):
    """Float distance from centre to the closed polygon (0 inside)."""
    return 0.0 if min(float_offsets(polygon, centre)) >= 0 else float_boundary_distance(polygon, centre)


def plan_box(polygon):
    xs, ys = [float(vertex[0]) for vertex in polygon], [float(vertex[1]) for vertex in polygon]
    return min(xs), min(ys), max(xs), max(ys)


def apart(box, centre, reach):
    x, y, margin = float(centre[0]), float(centre[1]), float(reach) + SCREEN_CM
    return x + margin < box[0] or box[2] < x - margin or y + margin < box[1] or box[3] < y - margin


def interiors_overlap(first, second):
    """Convex polygons share interior points unless an edge of one has the other wholly on or outside its line."""
    for ring, other in ((first, second), (second, first)):
        for a, b in edges(ring):
            if all(cross(sub(b, a), sub(p, a)) <= 0 for p in other):
                return False
    return True


def segment_enters(start, end, polygon):
    """The segment passes through the convex polygon's open interior."""
    lower, upper = Fraction(0), Fraction(1)
    for a, b in edges(polygon):
        delta = sub(b, a)
        constant, slope = cross(delta, sub(start, a)), cross(delta, sub(end, start))
        if slope == 0:
            if constant <= 0:
                return False
        elif slope > 0:
            lower = max(lower, bounded(-constant / slope))
        else:
            upper = min(upper, bounded(-constant / slope))
    return lower < upper


class Terrain:
    """Flat levels, plateaus and ramps of the world payload, with the same rules as the native importer."""

    def __init__(self, elevation, minimum, maximum, claim=None):
        keys(elevation, {"level_step_cm", "plateaus", "ramps"}, "world.elevation")
        self.step = content._wire_fraction(elevation["level_step_cm"], "world level step")
        require(self.step > 0, "InvalidGeometry", "world.elevation", "a positive level step is required")
        self.plateaus, self.ramps, self.boxes = {}, {}, {}
        for kind, level_key in (("plateaus", "level"), ("ramps", "lower_level")):
            rows = elevation[kind]
            require(isinstance(rows, (list, tuple)) and len(rows) <= ELEVATION_ROWS, "InvalidGeometry", f"world.{kind}",
                    f"at most {ELEVATION_ROWS} {kind} are supported")
            require([row["id"] for row in rows] == sorted(row["id"] for row in rows),
                    "NonCanonical", f"world.{kind}", "elevation rows must be ordinal-sorted by ID")
            for row in rows:
                keys(row, {"id", level_key, "polygon_cm"}, f"world.{kind}")
                if claim:
                    claim(row["id"], f"world.{kind}")
                level = row[level_key]
                low, high = (1, MAX_LEVEL) if kind == "plateaus" else (0, MAX_LEVEL - 1)
                integer(level, f"{row['id']}.{level_key}", low, high)
                polygon = tuple(point(vertex, row["id"]) for vertex in row["polygon_cm"])
                require(all(minimum[i] <= vertex[i] <= maximum[i] for vertex in polygon for i in (0, 1)),
                        "InvalidGeometry", row["id"], "elevation footprint leaves the surface")
                validate_polygon(polygon, row["id"])
                self.boxes[row["id"]] = plan_box(polygon)
                if kind == "plateaus":
                    require(all(vertex[2] == self.height(level) for vertex in polygon),
                            "InvalidGeometry", row["id"], "plateau vertex must lie at its level's height")
                    self.plateaus[row["id"]] = (level, polygon)
                else:
                    self._ramp(row["id"], level, polygon)
        self.highest = max((level for level, _ in self.plateaus.values()), default=0)

    def height(self, level):
        return bounded(self.step * level)

    def _ramp(self, identity, lower, polygon):
        require(len(polygon) == 4, "InvalidGeometry", identity, "a ramp is a four-vertex polygon")
        a, b, c, d = polygon
        require(a[2] == b[2] == self.height(lower) and c[2] == d[2] == self.height(lower + 1), "InvalidGeometry", identity,
                "ramp low edge must stand at its lower level and high edge at the next")
        low_edge, high_edge = sub(b[:2], a[:2]), sub(c[:2], d[:2])
        require(cross(low_edge, high_edge) == 0 and dot(low_edge, high_edge) > 0, "InvalidGeometry", identity,
                "ramp low and high edges must be parallel")
        run = cross(low_edge, sub(d[:2], a[:2]))
        require(bounded(self.step * self.step * dot(low_edge, low_edge)) <= bounded(RAMP_SLOPE * RAMP_SLOPE * run * run),
                "InvalidGeometry", identity, "ramp is steeper than bodies can walk")
        joined, supported = False, lower == 0
        for level, plateau in self.plateaus.values():
            require(not (level > lower and interiors_overlap(plateau, polygon)), "InvalidGeometry", identity,
                    "ramp overlaps higher ground")
            if level == lower and all(inside(vertex, plateau) for vertex in polygon):
                supported = True
            if level == lower + 1:
                for start, finish in edges(plateau):
                    edge = sub(finish[:2], start[:2])

                    def along(q):
                        t = dot(sub(q[:2], start[:2]), edge)
                        return cross(edge, sub(q[:2], start[:2])) == 0 and 0 <= t <= dot(edge, edge)

                    joined = joined or (along(c) and along(d) and cross(edge, sub(a[:2], start[:2])) < 0)
        require(joined, "InvalidGeometry", identity, "ramp high edge must lie along an edge of a plateau one level up")
        require(supported, "InvalidGeometry", identity, "ramp above the ground must stand wholly on a plateau of its lower level")
        for _, other in self.ramps.values():
            require(not interiors_overlap(other, polygon), "InvalidGeometry", identity, "ramps overlap")
        self.ramps[identity] = (lower, polygon)

    def disc_level(self, centre, radius):
        """Flat level a disc stands on wholly, or None when it meets a cliff, higher ground or a ramp."""
        near = [(height, plateau) for identity, (height, plateau) in self.plateaus.items()
                if not apart(self.boxes[identity], centre, radius)]
        level = max((height for height, plateau in near if disc_inside(plateau, centre, radius)), default=0)
        if any(height > level and disc_overlaps(plateau, centre, radius) for height, plateau in near):
            return None
        if any(not apart(self.boxes[identity], centre, radius) and disc_overlaps(ramp, centre, radius)
               for identity, (_, ramp) in self.ramps.items()):
            return None
        return level

    def ring_level(self, ring):
        level = max((height for height, plateau in self.plateaus.values()
                     if all(inside(vertex, plateau) for vertex in ring)), default=0)
        if any(height > level and interiors_overlap(plateau, ring) for height, plateau in self.plateaus.values()):
            return None
        if any(interiors_overlap(ramp, ring) for _, ramp in self.ramps.values()):
            return None
        return level

    def tier(self, p):
        """Half levels: 2L on level L, 2L + 1 on a ramp from L; where regions meet the higher tier wins."""
        tiers = [2 * level for level, plateau in self.plateaus.values() if inside(p, plateau)]
        tiers += [2 * lower + 1 for lower, ramp in self.ramps.values() if inside(p, ramp)]
        return max(tiers, default=0)

    def regions(self):
        yield from ((2 * level, plateau) for level, plateau in self.plateaus.values())
        yield from ((2 * lower + 1, ramp) for lower, ramp in self.ramps.values())


class Geometry:
    def __init__(self, world):
        surface = world["geometry"]["surfaces"][0]
        self.minimum = point(surface["min_cm"], surface["id"])
        self.maximum = point(surface["max_cm"], surface["id"])
        blockers = world["geometry"]["blockers"]
        self.polygons = {row["id"]: tuple(point(p, row["id"]) for p in row["polygon_cm"])
                         for row in blockers if row["shape"] == "polygon"}
        # Upright cylinders by footprint: (centre, radius).
        self.circles = {row["id"]: (point(row["center_cm"], row["id"]),
                                    content._wire_fraction(row["radius_cm"], row["id"]))
                        for row in blockers if row["shape"] == "cylinder"}
        self.rows = {row["id"]: row for row in blockers}
        self.boxes = {identity: plan_box(polygon) for identity, polygon in self.polygons.items()}
        # Float footprints for the screens: (x, y, radius, exact centre, exact radius).
        self.float_circles = [(float(centre[0]), float(centre[1]), float(size), centre, size)
                              for centre, size in self.circles.values()]
        self.terrain = Terrain(world["geometry"]["elevation"], self.minimum, self.maximum)
        # Level each blocker stands on; a blocker only blocks the sight of observers at or below its level.
        self.levels = {identity: self.terrain.ring_level(polygon) for identity, polygon in self.polygons.items()}
        self.levels.update({identity: self.terrain.disc_level(centre, size) for identity, (centre, size) in self.circles.items()})

    def standing(self, p, radius):
        """Ground height of a disc wholly on one flat level, or None."""
        level = self.terrain.disc_level(p, radius)
        return None if level is None else self.terrain.height(level)

    def legal(self, p, radius=Fraction(0)):
        reach, x, y = float(radius), float(p[0]), float(p[1])
        return (p[2] == self.standing(p, radius)
                and all(self.minimum[i] + radius <= p[i] <= self.maximum[i] - radius for i in (0, 1))
                and all(apart(self.boxes[identity], p, radius) or self._clear_of(p, radius, polygon)
                        for identity, polygon in self.polygons.items())
                and all(math.hypot(x - cx, y - cy) > reach + cr + SCREEN_CM
                        or squared_distance(p, centre) >= bounded((size + radius) ** 2)
                        for cx, cy, cr, centre, size in self.float_circles))

    def _clear_of(self, p, radius, polygon):
        """p is not strictly inside polygon and at least radius from it; floats settle it unless within SCREEN_CM."""
        inner, gap = min(float_offsets(polygon, p)), float_boundary_distance(polygon, p)
        if inner > SCREEN_CM:
            return False
        if inner < -SCREEN_CM and abs(gap - float(radius)) > SCREEN_CM:
            return gap > float(radius)
        return not inside(p, polygon, strict=True) and polygon_distance_squared(p, polygon) >= bounded(radius * radius)

    def clearance(self, p):
        distances = [p[i] - self.minimum[i] for i in (0, 1)] + [self.maximum[i] - p[i] for i in (0, 1)]
        x, y = float(p[0]), float(p[1])
        # Floats rank every blocker; only those within SCREEN_CM of the nearest are measured exactly, so the reported
        # clearance is the exact one.
        rough = [(float_polygon_distance(polygon, p), polygon, None) for polygon in self.polygons.values()]
        rough += [(math.hypot(x - cx, y - cy) - cr, centre, size) for cx, cy, cr, centre, size in self.float_circles]
        nearest = min((value for value, _, _ in rough), default=math.inf)
        return min(*(float(value) for value in distances),
                   *(math.sqrt(float(polygon_distance_squared(p, shape))) if size is None
                     else math.sqrt(float(squared_distance(p, shape))) - float(size)
                     for value, shape, size in rough if value <= nearest + SCREEN_CM))

    def sight_clear(self, start, end):
        """#97 tiered sight: nothing on a higher tier is seen; higher terrain between hides; an obstacle hides only from
        observers at or below its level (a ramp observer counts as its lower level)."""
        observer, target = self.terrain.tier(start), self.terrain.tier(end)
        if target > observer:
            return False
        if any(tier > observer and segment_enters(start, end, region) for tier, region in self.terrain.regions()):
            return False
        level = observer // 2
        # A trunk blocks a segment that enters its open disc; a tangent is not an interior crossing.
        for identity, (centre, size) in self.circles.items():
            if not self.rows[identity]["blocks_sight"] or self.levels[identity] < level:
                continue
            reach = (squared_distance(start, centre) if start[:2] == end[:2]
                     else segment_distance_squared(centre, start, end))
            if reach < bounded(size * size):
                return False
        # Strict interior intersection; a tangent along a wall boundary is not an interior crossing.
        for identity, polygon in self.polygons.items():
            if not self.rows[identity]["blocks_sight"] or self.levels[identity] < level:
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


def validate_bindings(world, data):
    anchors = content.rows_by_id(world["anchors"], "world anchors")
    content.validate_world_references(data, world, anchors)
    for key in PARAMETER_REFS:
        require(key in data["parameters"], "InvalidReference", key, "unresolved central parameter")
    p = lambda key: content.quantity(data["parameters"][key])
    radius = p("UNIT.CollisionRadiusCm")
    geometry = Geometry(world)
    capture = point(anchors[data["world"]["capture_id"]]["position_cm"], "capture")
    shops = {identity: point(anchors[identity]["position_cm"], identity) for identity in data["world"]["shop_ids"]}
    capture_radius, shop_radius = p("CAPTURE.RadiusCm"), p("SHOP.InteractRadiusCm")
    require(geometry.legal(capture, radius + capture_radius), "InvalidGeometry", "capture", "capture disk lacks legal capsule support")
    for identity, shop in shops.items():
        require(squared_distance(capture, shop) > bounded((capture_radius + shop_radius) ** 2),
                "InvalidGeometry", identity, "shop eligibility and capture disks overlap")
        for other, elsewhere in shops.items():
            require(other == identity or squared_distance(shop, elsewhere) > bounded((2 * shop_radius) ** 2),
                    "InvalidGeometry", identity, "two shops' eligibility disks overlap")
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
        for other, other_radius in ((capture, capture_radius), *((shop, shop_radius) for shop in shops.values())):
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
    # Each shop anchor stands in its own low counter: a polygon blocker that stops movement but not sight.
    counters = set()
    for identity, shop in shops.items():
        stalls = [stall for stall, row in geometry.rows.items()
                  if not row["blocks_sight"] and stall in geometry.polygons and inside(shop, geometry.polygons[stall])]
        require(len(stalls) == 1 and stalls[0] not in counters, "InvalidGeometry", identity,
                "shop anchor must stand in exactly one low non-sight-blocking stall of its own")
        counters.add(stalls[0])
    offsets = [point(value, "spawn offset") for value in world["placement"]["spawn_offsets_cm"]]
    for i, offset in enumerate(offsets):
        for other in offsets[:i]:
            require(squared_distance(offset, other) >= bounded((2 * radius) ** 2),
                    "WorldPlacement", "spawn offsets", "local placements overlap at the current capsule radius")
    clear_radius = content._wire_fraction(world["geometry"]["camp_clearance_cm"], "camp clearance")
    reset_radius = p("CAMP.ResetArrivalRadiusCm")
    monsters = content.rows_by_id(data["camps"]["monsters"], "monsters")
    placements = {row["anchor_id"]: monsters[row["monster_id"]] for row in data["camps"]["placements"]}
    candidates, home_clearance = {}, {}
    for identity, row in anchors.items():
        location = point(row["position_cm"], identity)
        if row["kind"] == "Shop":
            continue
        require(geometry.legal(location, radius), "InvalidGeometry", identity, "anchor has no legal static capsule position")
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
    require(len(boundary_walls) == 4, "InvalidGeometry", "boundary", "each surface edge needs its own boundary wall")
    # Every other blocker stands on the ground it occupies, wholly inside the surface.
    for identity, polygon in geometry.polygons.items():
        if identity not in boundary_walls:
            require(all(geometry.minimum[i] <= vertex[i] <= geometry.maximum[i] for vertex in polygon for i in (0, 1)),
                    "InvalidGeometry", identity, "inner blocker leaves its surface")
    for identity, (centre, size) in geometry.circles.items():
        require(all(geometry.minimum[i] + size <= centre[i] <= geometry.maximum[i] - size for i in (0, 1)),
                "InvalidGeometry", identity, "inner blocker leaves its surface")
    return {"home_clearance_cm": home_clearance, "spawn_candidates": len(candidates), "member_homes": len(home_clearance)}


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
