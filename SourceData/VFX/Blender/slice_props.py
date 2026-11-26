# Thrown and placed skill props for the presentation slice: small static meshes a skill's visible flight or placed
# object is drawn with, in place of an engine cube. Each has two material slots, Body and Trim, which the game tints.
#   * SM_LD_PropTakeout  a tied takeout bag with its box inside and a label band (flight body; origin at its centre)
#   * SM_LD_PropSlipper  a flip-flop lying on the floor: sole, and its strap as trim (origin on the floor, toe toward +X)
#   * SM_LD_PropBag      a school backpack standing on the floor: body, and pocket, lid, handle and straps as trim; 100 cm
#                        wide and deep and 120 cm high so a blocker's radius and height scale it directly
#   * SM_LD_PropBrick    a house brick with a frog and three holes, mortar still stuck to it as trim (origin at its centre)
#   * SM_LD_PropFlyer    a sheet of paper lying on the floor with two curled corners, its print as trim (origin on the floor)
#   * SM_LD_PropTape     a metre of barrier tape hanging between its ends at z = 0 with a sag and a slight twist, striped
#                        Body / Trim along its length (origin at its middle; scaled along X to the span)
# Centimetres, X forward, Z up, flat shaded. Run from the repository root (writes SourceData/VFX/Slice/*.fbx and prints
# every file's SHA-256, geometry SHA-256 and counts for slice_manifest.py):
#   blender -b --factory-startup -P SourceData/VFX/Blender/slice_props.py -- SourceData/VFX/Slice [previewDir]
import hashlib, math, os, sys
import bpy, bmesh
from mathutils import Matrix, Vector

ARGS = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
OUT = os.path.abspath(ARGS[0] if ARGS else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Slice"))
PREVIEW = os.path.abspath(ARGS[1]) if len(ARGS) > 1 else None
os.makedirs(OUT, exist_ok=True)

BODY, TRIM = 0, 1


class part:
    """Faces made inside the block take the material slot."""

    def __init__(self, bm, slot):
        self.bm, self.slot = bm, slot

    def __enter__(self):
        self.before = set(self.bm.faces)

    def __exit__(self, *exc):
        for face in self.bm.faces:
            if face not in self.before:
                face.material_index = self.slot


def box(bm, size, at, bevel=0.0, taper=1.0, segments=2, turn=None):
    """A box of size (x, y, z) centred at `at`; taper scales the top face in XY; bevel rounds every edge."""
    made = bmesh.ops.create_cube(bm, size=1.0)
    verts = made["verts"]
    for v in verts:
        top = v.co.z > 0
        p = Vector((v.co.x * size[0] * (taper if top else 1.0), v.co.y * size[1] * (taper if top else 1.0), v.co.z * size[2]))
        if turn is not None:
            p = turn @ p
        v.co = p + Vector(at)
    if bevel > 0:
        edges = list({e for v in verts for e in v.link_edges})
        bmesh.ops.bevel(bm, geom=edges, offset=bevel, segments=segments, profile=0.5, affect="EDGES")


def tube(bm, points, radius, sides=6):
    """A closed-end tube through the points (a strap or a handle)."""
    rings = []
    for i, p in enumerate(points):
        p = Vector(p)
        ahead = (Vector(points[min(i + 1, len(points) - 1)]) - Vector(points[max(i - 1, 0)])).normalized()
        side = ahead.cross(Vector((0, 0, 1)))
        if side.length < 1e-3:
            side = ahead.cross(Vector((0, 1, 0)))
        side.normalize()
        up = side.cross(ahead).normalized()
        rings.append([bm.verts.new(p + (side * math.cos(a) + up * math.sin(a)) * radius) for a in [2 * math.pi * k / sides for k in range(sides)]])
    for a, b in zip(rings, rings[1:]):
        for k in range(sides):
            bm.faces.new([a[k], a[(k + 1) % sides], b[(k + 1) % sides], b[k]])
    bm.faces.new(list(reversed(rings[0])))
    bm.faces.new(rings[-1])


def arc(centre, radius, plane, begin, end, steps):
    """Points on an arc; plane picks the two axes ('xz', 'yz' or 'xy')."""
    axes = {"xz": (0, 2), "yz": (1, 2), "xy": (0, 1)}[plane]
    out = []
    for i in range(steps + 1):
        a = math.radians(begin + (end - begin) * i / steps)
        p = list(centre)
        p[axes[0]] += radius * math.cos(a)
        p[axes[1]] += radius * math.sin(a)
        out.append(tuple(p))
    return out


def takeout(bm):
    # The box inside the bag, the bag's slack gathered above it, the knot and its two ears; a label band round the box.
    with part(bm, BODY):
        box(bm, (40, 30, 20), (0, 0, -6), bevel=3.0, taper=1.08)
        box(bm, (30, 22, 12), (0, 0, 9), bevel=4.0, taper=0.45)
        box(bm, (9, 9, 7), (0, 0, 18), bevel=2.5)
        tube(bm, [(0, 6, 20), (0, 15, 27), (0, 20, 23)], 2.4)
        tube(bm, [(0, -6, 20), (0, -15, 27), (0, -20, 23)], 2.4)
    with part(bm, TRIM):
        box(bm, (42.5, 32.5, 7), (0, 0, -8), bevel=1.0)
        tube(bm, arc((0, 0, 20), 9, "yz", 20, 160, 6), 2.2)


def slipper(bm):
    # The sole: an outline wider at the toe than the heel, 7 cm thick, with the heel a little higher.
    with part(bm, BODY):
        outline = []
        steps = 20
        for i in range(steps):
            a = 2 * math.pi * i / steps
            c, s = math.cos(a), math.sin(a)
            half = 15.0 + 5.0 * max(c, 0.0) - 2.0 * max(-c, 0.0)
            outline.append((34.0 * c, half * s))
        low = [bm.verts.new((x, y, 0.0)) for x, y in outline]
        high = [bm.verts.new((x * 0.96, y * 0.94, 7.0 + (1.5 if x < 0 else 0.0))) for x, y in outline]
        for k in range(steps):
            bm.faces.new([low[k], low[(k + 1) % steps], high[(k + 1) % steps], high[k]])
        bm.faces.new(list(reversed(low)))
        bm.faces.new(high)
    # The strap: a post between the toes and two bands arching back to the sides.
    with part(bm, TRIM):
        tube(bm, [(18, 0, 6), (17, 0, 13)], 2.0)
        tube(bm, [(17, 0, 12), (8, 8, 17), (-2, 15, 12), (-6, 17, 6)], 2.6)
        tube(bm, [(17, 0, 12), (8, -8, 17), (-2, -15, 12), (-6, -17, 6)], 2.6)


def bag(bm):
    # The body (rounded, narrower toward the top); the lid's fold, the front pocket, the top handle and two shoulder straps.
    with part(bm, BODY):
        box(bm, (62, 88, 104), (0, 0, 52), bevel=12.0, taper=0.84, segments=3)
    with part(bm, TRIM):
        box(bm, (66, 82, 16), (2, 0, 96), bevel=6.0, taper=0.8)
        box(bm, (16, 62, 46), (36, 0, 34), bevel=6.0, taper=0.9)
        tube(bm, arc((0, 0, 108), 12, "yz", 0, 180, 6), 3.2)
        for side in (-1, 1):
            tube(bm, [(-30, 24 * side, 98), (-42, 26 * side, 78), (-44, 28 * side, 46), (-36, 30 * side, 16), (-28, 32 * side, 8)], 4.2)


def brick(bm):
    # A brick three times life size so it reads from the gameplay camera: 66 x 32 x 20, edges worn round, three holes
    # through it shown as dark wells on both faces, and lumps of old mortar on one end and one side.
    with part(bm, BODY):
        box(bm, (66, 32, 20), (0, 0, 0), bevel=2.2, segments=2)
    with part(bm, TRIM):
        for x in (-20, 0, 20):
            box(bm, (11, 13, 20.6), (x, 0, 0), bevel=3.0)
        box(bm, (5, 27, 15), (33.5, 1, -1), bevel=2.0, taper=0.8)
        box(bm, (30, 4, 12), (-12, 16.6, -2), bevel=1.8, taper=0.7)
        box(bm, (16, 9, 3.5), (18, -8, 10.6), bevel=1.4, taper=0.6)


def flyer(bm):
    # A sheet 84 x 60 on the floor, two opposite corners curled up; its print: a header block and three lines.
    columns, rows = 10, 8
    def height(u, v):
        near = max(0.0, 1.0 - math.hypot(u - 1.0, v - 1.0) / 0.55)
        far = max(0.0, 1.0 - math.hypot(u, v) / 0.4)
        return 0.6 + 16.0 * near ** 2 + 9.0 * far ** 2 + 1.2 * math.sin(u * 7.0) * math.sin(v * 5.0)
    def at(u, v, lift=0.0):
        return ((u - 0.5) * 84.0, (v - 0.5) * 60.0, height(u, v) + lift)
    def patch(u0, u1, v0, v1, lift):
        nu, nv = max(2, int((u1 - u0) * columns) + 1), max(2, int((v1 - v0) * rows) + 1)
        grid = [[bm.verts.new(at(u0 + (u1 - u0) * i / nu, v0 + (v1 - v0) * j / nv, lift)) for j in range(nv + 1)] for i in range(nu + 1)]
        for i in range(nu):
            for j in range(nv):
                quad = [grid[i][j], grid[i + 1][j], grid[i + 1][j + 1], grid[i][j + 1]]
                bm.faces.new(quad)
                bm.faces.new([bm.verts.new(v.co) for v in reversed(quad)])
    with part(bm, BODY):
        patch(0.0, 1.0, 0.0, 1.0, 0.0)
    with part(bm, TRIM):
        patch(0.08, 0.92, 0.66, 0.9, 0.5)
        for v in (0.46, 0.3, 0.14):
            patch(0.08, 0.8 if v > 0.2 else 0.55, v, v + 0.07, 0.5)


def tape(bm):
    # A metre of tape between x = -50 and x = 50: 14 cm tall, sagging 9 cm at the middle with a twist that runs along
    # it; alternate segments are Body and Trim (the stripes). Both faces are drawn.
    segments = 20
    def edge(u):
        sag = -9.0 * 4.0 * u * (1.0 - u)
        roll = math.radians(16.0) * math.sin(u * math.pi * 2.0)
        up = Vector((0.0, math.sin(roll), math.cos(roll))) * 7.0
        mid = Vector(((u - 0.5) * 100.0, 0.0, sag))
        return mid - up, mid + up
    for i in range(segments):
        with part(bm, BODY if i % 2 == 0 else TRIM):
            a0, a1 = edge(i / segments)
            b0, b1 = edge((i + 1) / segments)
            # Stripes lean: the upper edge runs half a segment ahead.
            lean = Vector((100.0 / segments * 0.5, 0, 0))
            quad = [a0, b0, b1 + lean if i < segments - 1 else b1, a1 + lean if i > 0 else a1]
            bm.faces.new([bm.verts.new(p) for p in quad])
            bm.faces.new([bm.verts.new(p) for p in reversed(quad)])


PROPS = {"SM_LD_PropTakeout": takeout, "SM_LD_PropSlipper": slipper, "SM_LD_PropBag": bag, "SM_LD_PropBrick": brick, "SM_LD_PropFlyer": flyer,
         "SM_LD_PropTape": tape}


def build(name, make):
    mesh = bpy.data.meshes.new(name)
    for slot in ("Body", "Trim"):
        material = bpy.data.materials.get(slot) or bpy.data.materials.new(slot)
        mesh.materials.append(material)
    bm = bmesh.new()
    bm.loops.layers.uv.new("UVMap")
    make(bm)
    bmesh.ops.recalc_face_normals(bm, faces=[f for f in bm.faces if name not in ("SM_LD_PropFlyer", "SM_LD_PropTape")])
    bmesh.ops.triangulate(bm, faces=bm.faces[:])
    bm.to_mesh(mesh)
    bm.free()
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.scene.collection.objects.link(obj)
    for poly in mesh.polygons:
        poly.use_smooth = False
    return obj


def geometry_sha(mesh):
    digest = hashlib.sha256()
    for v in mesh.vertices:
        digest.update(("%.3f,%.3f,%.3f;" % tuple(v.co)).encode())
    for p in mesh.polygons:
        digest.update((",".join(str(i) for i in p.vertices) + ":%d;" % p.material_index).encode())
    return digest.hexdigest()


def export_fbx(obj, path):
    for other in bpy.context.scene.objects:
        other.select_set(False)
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.export_scene.fbx(filepath=path, use_selection=True, object_types={"MESH"}, apply_unit_scale=True,
                             global_scale=1.0, axis_forward="X", axis_up="Z", mesh_smooth_type="FACE",
                             add_leaf_bones=False, bake_anim=False, use_mesh_modifiers=True, path_mode="STRIP")


def preview(objects, out_path):
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.render.resolution_x, scene.render.resolution_y = 1800, 520
    scene.render.image_settings.file_format = "PNG"
    scene.display.shading.light = "STUDIO"
    scene.display.shading.color_type = "MATERIAL"
    scene.display.shading.show_cavity = True
    bpy.data.materials["Body"].diffuse_color = (0.72, 0.62, 0.42, 1.0)
    bpy.data.materials["Trim"].diffuse_color = (0.3, 0.16, 0.1, 1.0)
    x = 0.0
    for obj in objects:
        obj.location = (0, x, 0)
        x += 125.0
    cam = bpy.data.cameras.new("View")
    cam.type = "ORTHO"
    cam.ortho_scale = 760.0
    rig = bpy.data.objects.new("View", cam)
    scene.collection.objects.link(rig)
    scene.camera = rig
    # The gameplay camera looks down at roughly 55 degrees.
    rig.rotation_euler = (math.radians(42), 0, math.radians(90))
    rig.location = Vector((0, 312, 40)) + Matrix.Rotation(math.radians(90), 4, "Z") @ Vector((0, -600 * math.sin(math.radians(42)), 600 * math.cos(math.radians(42))))
    scene.render.filepath = out_path
    bpy.ops.render.render(write_still=True)


def main():
    bpy.context.scene.unit_settings.scale_length = 0.01
    for obj in list(bpy.data.objects):
        bpy.data.objects.remove(obj)
    built = []
    for name, make in PROPS.items():
        obj = build(name, make)
        path = os.path.join(OUT, name + ".fbx")
        export_fbx(obj, path)
        with open(path, "rb") as handle:
            print("PROP %s sha256=%s geometry_sha256=%s vertices=%d triangles=%d" % (
                name, hashlib.sha256(handle.read()).hexdigest(), geometry_sha(obj.data), len(obj.data.vertices), len(obj.data.polygons)))
        built.append(obj)
    if PREVIEW:
        os.makedirs(PREVIEW, exist_ok=True)
        preview(built, os.path.join(PREVIEW, "props.png"))


main()
