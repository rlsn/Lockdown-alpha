# Basic-attack combo slash assets for the presentation slice.
#   * three slash-ribbon static meshes (FBX, cm, X forward, Z up, pivot = swing origin = chest height, arc in the XY plane)
#   * the slash texture (RGB glow / A coverage) and a tileable erosion-noise mask
# Run from the repository root (writes SourceData/VFX/Slice/*, prints every file's SHA-256):
#   blender -b --factory-startup -P SourceData/VFX/Blender/slice_slash.py -- SourceData/VFX/Slice [previewDir]
# previewDir (optional) receives top-down Blender renders of each mesh textured with the slash mask (white tint).
#
# UV convention (as seen in Unreal): U runs along the arc 0 = head (leading end of the swing) -> 1 = tail, V runs across
# the ribbon 0 = inner edge -> 1 = outer cutting edge. Blender stores V flipped for FBX, so the mesh holds 1 - V.
# Swing direction is stated relative to forward (+X): stage 1 right -> left, stage 2 left -> right, stage 3 right -> left.
import hashlib, math, os, sys
import numpy as np
import bpy, bmesh

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import slice_lib as L

ARGS = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
OUT = os.path.abspath(ARGS[0] if ARGS else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Slice"))
PREVIEW = os.path.abspath(ARGS[1]) if len(ARGS) > 1 else None
os.makedirs(OUT, exist_ok=True)

COLUMNS, ROWS = 72, 4  # quads along the arc / across the ribbon

# Per stage: ellipse semi-axes (cm, x = forward), head/tail angles in degrees from +X (positive = left of forward),
# maximum ribbon width, head ramp length (in U), taper exponent, leading-edge bulge, optional echo blade.
STAGES = {
    1: dict(name="SM_LD_SlashCombo1", a=96.0, b=58.0, head=+52.0, tail=-58.0, width=26.0, ramp=0.62, taper=1.2, bulge=0.0, echo=None),
    2: dict(name="SM_LD_SlashCombo2", a=106.0, b=66.0, head=-56.0, tail=+62.0, width=30.0, ramp=0.58, taper=1.35, bulge=0.0, echo=None),
    3: dict(name="SM_LD_SlashCombo3", a=128.0, b=104.0, head=+84.0, tail=-100.0, width=64.0, ramp=0.5, taper=1.0, bulge=0.4,
            echo=dict(grow=26.0, head=+70.0, tail=-52.0, width=16.0, ramp=0.6, taper=1.4)),
}


def profile(u, width, ramp, taper, bulge):
    """Crescent width profile: pointed head (u = 0), peak near u ~ 0.3 (ramp shifts it), pointed tail. taper > 1 lengthens the tail."""
    peak = 0.5 ** (1.0 / ramp)  # u at which sin(pi * u^ramp) peaks
    base = np.sin(math.pi * np.clip(u, 0, 1) ** ramp) ** 0.85
    tailfix = (1.0 - u) ** (taper - 1.0)
    heavy = 1.0 + bulge * np.exp(-(((u - 0.2) / 0.16) ** 2))
    return width * base * tailfix * heavy


def ribbon(a, b, head_deg, tail_deg, width, ramp, taper, bulge, offset=0):
    """Quad strip: outer edge on the ellipse (constant sharp cutting edge), inner edge pulled inward by the profile."""
    verts, uvs, faces = [], [], []
    for i in range(COLUMNS + 1):
        u = i / COLUMNS
        ang = math.radians(head_deg + (tail_deg - head_deg) * u)
        outer = np.array([a * math.cos(ang), b * math.sin(ang)])
        normal = np.array([math.cos(ang) / a, math.sin(ang) / b])
        normal /= np.linalg.norm(normal)
        w = float(profile(np.array(u), width, ramp, taper, bulge))
        for j in range(ROWS + 1):
            v = j / ROWS  # 0 inner .. 1 outer
            p = outer - normal * w * (1.0 - v)
            verts.append((float(p[0]), float(p[1]), 0.0))
            uvs.append((u, 1.0 - v))  # Blender V = 1 - Unreal V
    for i in range(COLUMNS):
        for j in range(ROWS):
            k = i * (ROWS + 1) + j
            faces.append((k, k + 1, k + 1 + (ROWS + 1), k + (ROWS + 1)))
    return verts, uvs, faces


def build_mesh(stage, spec):
    parts = [ribbon(spec["a"], spec["b"], spec["head"], spec["tail"], spec["width"], spec["ramp"], spec["taper"], spec["bulge"])]
    if spec["echo"]:
        e = spec["echo"]
        parts.append(ribbon(spec["a"] + e["grow"], spec["b"] + e["grow"], e["head"], e["tail"], e["width"], e["ramp"], e["taper"], 0.0))
    mesh = bpy.data.meshes.new(spec["name"])
    bm = bmesh.new()
    uv_layer = bm.loops.layers.uv.new("UVMap")
    for verts, uvs, faces in parts:
        bverts = [bm.verts.new(v) for v in verts]
        for f in faces:
            face = bm.faces.new([bverts[i] for i in f])
            for loop, idx in zip(face.loops, f):
                loop[uv_layer].uv = uvs[idx]
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    for face in bm.faces:
        if face.normal.z < 0:  # all faces up (+Z); the material is two-sided anyway
            face.normal_flip()
    bm.to_mesh(mesh)
    bm.free()
    obj = bpy.data.objects.new(spec["name"], mesh)
    bpy.context.scene.collection.objects.link(obj)
    for poly in mesh.polygons:
        poly.use_smooth = False
    return obj


def export_fbx(obj, path):
    for other in bpy.context.scene.objects:
        other.select_set(False)
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.export_scene.fbx(filepath=path, use_selection=True, object_types={"MESH"}, apply_unit_scale=True,
                             global_scale=1.0, axis_forward="X", axis_up="Z", mesh_smooth_type="FACE",
                             add_leaf_bones=False, bake_anim=False, use_mesh_modifiers=True, path_mode="STRIP")


# ---------------------------------------------------------------- slash texture (U along the arc, V across)
def slash_texture(width=512, height=128):
    u = ((np.arange(width, dtype=np.float32) + 0.5) / width)[None, :].repeat(height, 0)
    v = ((np.arange(height, dtype=np.float32) + 0.5) / height)[:, None].repeat(width, 1)  # row 0 = inner (V = 0)
    s1 = L.fbm(width, height, 3, 44, 11, 3)
    s2 = L.fbm(width, height, 2, 30, 23, 3)
    s3 = L.fbm(width, height, 4, 52, 37, 2)
    inner = 1.0 - v
    # body: element-colour streaks that thin out toward the inner edge and the tail
    alive = (1.2 - 1.15 * u) + (s1 - 0.5) * 1.0 - inner * 1.0
    body = L.smoothstep(0.0, 0.14, alive)
    # white-hot band inside the cutting edge, longest near the head, dropping out toward the inner side
    hot_alive = (1.0 - 1.3 * u) + (s2 - 0.5) * 0.5 - np.clip(0.64 - v, 0, 1) * 3.2
    core = L.smoothstep(0.0, 0.10, hot_alive)
    # ink: wide hard dark band outside the cutting edge (reads at ~0.5 px/cm), dark hairline inside, dark speed streaks
    edge_alive = L.smoothstep(0.0, 0.10, (1.3 - 1.15 * u) + (s1 - 0.5) * 0.8)
    outer_band = L.smoothstep(0.875, 0.89, v) * edge_alive
    streak_ink = L.smoothstep(0.58, 0.66, s3) * L.smoothstep(0.0, 0.1, (0.95 - 1.1 * u) - inner * 0.6 + (s1 - 0.5) * 0.4) * 0.9
    inner_band = (1.0 - L.smoothstep(0.045, 0.075, v)) * L.smoothstep(0.0, 0.1, (0.7 - 1.1 * u) + (s1 - 0.5) * 0.7)
    body = body * (1.0 - outer_band) * (1.0 - inner_band)
    core = core * (1.0 - outer_band) * (1.0 - inner_band)
    ink = np.clip(np.maximum.reduce([body, outer_band, streak_ink, inner_band]), 0, 1)
    return L.pack_layers(ink, body, core)


def erosion_noise(size=256):
    r = L.stretch01(L.fbm(size, size, 4, 4, 5, 4))                 # blobby dissolve
    g = L.stretch01(L.fbm(size, size, 2, 28, 7, 3))                # streaks along U (texture X)
    b = L.stretch01(L.worley(size, size, 7, 9))                    # cellular crackle
    return np.dstack([r, g, b, np.ones_like(r)])


# ---------------------------------------------------------------- preview (Blender top-down render, texture on the mesh)
def preview_render(obj, tex_path, out_path):
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_EEVEE"
    scene.render.resolution_x = scene.render.resolution_y = 768
    scene.render.film_transparent = True
    scene.view_settings.view_transform = "Standard"
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGBA"
    world = bpy.data.worlds.new("Black")
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs["Strength"].default_value = 0.0
    scene.world = world
    cam = bpy.data.cameras.new("Top")
    cam.type = "ORTHO"
    cam.ortho_scale = 340.0
    rig = bpy.data.objects.new("Top", cam)
    rig.location = (60, 0, 400)
    scene.collection.objects.link(rig)
    scene.camera = rig
    mat = bpy.data.materials.new("SlashPreview")
    mat.use_nodes = True
    nt = mat.node_tree
    nt.nodes.clear()
    img = bpy.data.images.load(tex_path)
    img.colorspace_settings.name = "Non-Color"
    tex = nt.nodes.new("ShaderNodeTexImage")
    tex.image = img
    emit = nt.nodes.new("ShaderNodeEmission")
    clear = nt.nodes.new("ShaderNodeBsdfTransparent")
    mix = nt.nodes.new("ShaderNodeMixShader")
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    nt.links.new(tex.outputs["Color"], emit.inputs["Color"])
    nt.links.new(tex.outputs["Alpha"], mix.inputs[0])
    nt.links.new(clear.outputs[0], mix.inputs[1])
    nt.links.new(emit.outputs[0], mix.inputs[2])
    nt.links.new(mix.outputs[0], out.inputs[0])
    obj.data.materials.clear()
    obj.data.materials.append(mat)
    scene.render.filepath = out_path
    bpy.ops.render.render(write_still=True)


def main():
    scene = bpy.context.scene
    scene.unit_settings.system = "METRIC"
    scene.unit_settings.scale_length = 0.01
    scene.unit_settings.length_unit = "CENTIMETERS"
    results = []
    for stage, spec in STAGES.items():
        obj = build_mesh(stage, spec)
        path = os.path.join(OUT, spec["name"] + ".fbx")
        export_fbx(obj, path)
        mesh = obj.data
        head_v = mesh.vertices[ROWS].co  # u = 0, outer edge
        tail_v = mesh.vertices[COLUMNS * (ROWS + 1) + ROWS].co
        # The FBX header embeds a creation timestamp, so the file bytes change per export; the geometry hash does not.
        digest = hashlib.sha256()
        for vert in mesh.vertices:
            digest.update(("%.3f,%.3f,%.3f;" % tuple(vert.co)).encode())
        for loop in mesh.uv_layers["UVMap"].data:
            digest.update(("%.5f,%.5f;" % tuple(loop.uv)).encode())
        for poly in mesh.polygons:
            digest.update((",".join(str(i) for i in poly.vertices) + ";").encode())
        print(f"LD_SLICE_MESH {spec['name']} verts={len(mesh.vertices)} tris={len(mesh.polygons) * 2} head_outer=({head_v.x:.1f},{head_v.y:.1f}) tail_outer=({tail_v.x:.1f},{tail_v.y:.1f}) geometry_sha256={digest.hexdigest()} file_sha256={L.sha256_file(path)}")
        results.append((stage, obj))
    tex_path = os.path.join(OUT, "tex_slash_edge.png")
    print("LD_SLICE_TEXTURE tex_slash_edge 512x128 sha256=" + L.write_png(tex_path, slash_texture()))
    noise_path = os.path.join(OUT, "tex_dissolve_noise.png")
    print("LD_SLICE_TEXTURE tex_dissolve_noise 256x256 sha256=" + L.write_png(noise_path, erosion_noise()))
    if PREVIEW:
        os.makedirs(PREVIEW, exist_ok=True)
        for stage, obj in results:
            for other in results:
                other[1].hide_render = other[0] != stage
            preview_render(obj, tex_path, os.path.join(PREVIEW, f"raw_mesh_stage{stage}.png"))


main()
