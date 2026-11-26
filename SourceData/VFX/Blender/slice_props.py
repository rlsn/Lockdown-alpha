# Thrown and placed skill props for the presentation slice: three small static meshes a skill's visible flight or placed
# object is drawn with, in place of an engine cube.
#   * SM_LD_PropTakeout  a tied takeout bag with its box inside (flight body; origin at its centre, +X = flight direction)
#   * SM_LD_PropSlipper  a flip-flop lying on the floor (origin at the floor under its centre, toe toward +X)
#   * SM_LD_PropBag      a school backpack standing on the floor (origin at the floor under its centre, front toward +X),
#                        100 cm wide and deep and 120 cm high so a blocker's radius and height scale it directly
# Centimetres, X forward, Z up, flat shaded, one material slot. Run from the repository root (writes
# SourceData/VFX/Slice/*.fbx and prints every file's SHA-256 and geometry SHA-256):
#   blender -b --factory-startup -P SourceData/VFX/Blender/slice_props.py -- SourceData/VFX/Slice [previewDir]
import hashlib, math, os, sys
import bpy, bmesh
from mathutils import Matrix, Vector

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

ARGS = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
OUT = os.path.abspath(ARGS[0] if ARGS else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Slice"))
PREVIEW = os.path.abspath(ARGS[1]) if len(ARGS) > 1 else None
os.makedirs(OUT, exist_ok=True)


def box(bm, size, at, bevel=0.0, taper=1.0, segments=2):
    """A box of size (x, y, z) centred at `at`; taper scales the top face in XY; bevel rounds every edge."""
    made = bmesh.ops.create_cube(bm, size=1.0)
    verts = made["verts"]
    for v in verts:
        top = v.co.z > 0
        v.co = Vector((v.co.x * size[0] * (taper if top else 1.0), v.co.y * size[1] * (taper if top else 1.0), v.co.z * size[2])) + Vector(at)
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
    # The box inside the bag, the bag's slack gathered above it, the knot and its two ears.
    box(bm, (40, 30, 20), (0, 0, -6), bevel=3.0, taper=1.08)
    box(bm, (30, 22, 12), (0, 0, 9), bevel=4.0, taper=0.45)
    box(bm, (9, 9, 7), (0, 0, 18), bevel=2.5)
    tube(bm, arc((0, 0, 20), 9, "yz", 20, 160, 6), 2.2)
    tube(bm, [(0, 6, 20), (0, 15, 27), (0, 20, 23)], 2.4)
    tube(bm, [(0, -6, 20), (0, -15, 27), (0, -20, 23)], 2.4)


def slipper(bm):
    # The sole: an outline wider at the toe than the heel, 7 cm thick, with the heel a little higher.
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
    tube(bm, [(18, 0, 6), (17, 0, 13)], 2.0)
    tube(bm, [(17, 0, 12), (8, 8, 17), (-2, 15, 12), (-6, 17, 6)], 2.6)
    tube(bm, [(17, 0, 12), (8, -8, 17), (-2, -15, 12), (-6, -17, 6)], 2.6)


def bag(bm):
    # The body (rounded, narrower toward the top), the lid's fold, the front pocket, the top handle and two shoulder straps.
    box(bm, (62, 88, 104), (0, 0, 52), bevel=12.0, taper=0.84, segments=3)
    box(bm, (66, 82, 16), (2, 0, 96), bevel=6.0, taper=0.8)
    box(bm, (16, 62, 46), (36, 0, 34), bevel=6.0, taper=0.9)
    tube(bm, arc((0, 0, 108), 12, "yz", 0, 180, 6), 3.2)
    for side in (-1, 1):
        tube(bm, [(-30, 24 * side, 98), (-42, 26 * side, 78), (-44, 28 * side, 46), (-36, 30 * side, 16), (-28, 32 * side, 8)], 4.2)


PROPS = {"SM_LD_PropTakeout": takeout, "SM_LD_PropSlipper": slipper, "SM_LD_PropBag": bag}


def build(name, make):
    mesh = bpy.data.meshes.new(name)
    bm = bmesh.new()
    bm.loops.layers.uv.new("UVMap")
    make(bm)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
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
        digest.update((",".join(str(i) for i in p.vertices) + ";").encode())
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
    scene.render.resolution_x, scene.render.resolution_y = 1500, 600
    scene.render.image_settings.file_format = "PNG"
    scene.display.shading.light = "STUDIO"
    scene.display.shading.show_cavity = True
    x = 0.0
    for obj in objects:
        obj.location = (0, x, 0)
        x += 130.0
    cam = bpy.data.cameras.new("View")
    cam.type = "ORTHO"
    cam.ortho_scale = 420.0
    rig = bpy.data.objects.new("View", cam)
    scene.collection.objects.link(rig)
    scene.camera = rig
    # The gameplay camera looks down at roughly 55 degrees.
    rig.rotation_euler = (math.radians(42), 0, math.radians(90))
    rig.location = Vector((0, 130, 40)) + Matrix.Rotation(math.radians(90), 4, "Z") @ Vector((0, -600 * math.sin(math.radians(42)), 600 * math.cos(math.radians(42))))
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
