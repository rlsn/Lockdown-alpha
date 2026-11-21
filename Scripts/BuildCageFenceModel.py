"""Builds one cage fence panel (1 m wide along X, 1.8 m tall, centred on X=0 and Y=0, standing on Z=0) and exports a GLB.

Usage: blender -b --factory-startup --python cage_fence.py -- <out.glb>
"""
import sys
import math
import bpy

out = sys.argv[-1]

bpy.ops.wm.read_factory_settings(use_empty=True)


def material(name, rgb, metallic, roughness, emission=0.0):
    m = bpy.data.materials.new(name)
    bsdf = m.node_tree.nodes['Principled BSDF']
    bsdf.inputs['Base Color'].default_value = (*rgb, 1.0)
    bsdf.inputs['Metallic'].default_value = metallic
    bsdf.inputs['Roughness'].default_value = roughness
    if emission > 0:
        bsdf.inputs['Emission Color'].default_value = (*rgb, 1.0)
        bsdf.inputs['Emission Strength'].default_value = emission
    return m


steel = material('CageSteel', (0.11, 0.11, 0.13), 0.85, 0.42)
accent = material('CageAccent', (0.85, 0.30, 0.95), 0.2, 0.35, 0.6)
parts = []


def box(size, loc, mat):
    bpy.ops.mesh.primitive_cube_add(size=1, location=loc)
    o = bpy.context.active_object
    o.scale = size
    o.data.materials.append(mat)
    parts.append(o)


def bar(radius, height, loc, mat, verts=8):
    bpy.ops.mesh.primitive_cylinder_add(vertices=verts, radius=radius, depth=height, location=loc)
    o = bpy.context.active_object
    o.data.materials.append(mat)
    parts.append(o)


def spike(radius, height, loc, mat):
    bpy.ops.mesh.primitive_cone_add(vertices=6, radius1=radius, radius2=0, depth=height, location=loc)
    o = bpy.context.active_object
    o.data.materials.append(mat)
    parts.append(o)


W, H = 1.0, 1.8
# Posts at both ends, slightly taller than the panel, with a cap.
for x in (-W / 2 + 0.03, W / 2 - 0.03):
    box((0.06, 0.06, H + 0.05), (x, 0, (H + 0.05) / 2), steel)
    box((0.08, 0.08, 0.03), (x, 0, H + 0.065), steel)
# Rails.
box((W, 0.035, 0.05), (0, 0, 0.12), steel)
box((W, 0.035, 0.05), (0, 0, H - 0.18), steel)
# The accent band under the spikes: the cage's colour at a glance.
box((W - 0.06, 0.04, 0.07), (0, 0, H - 0.05), accent)
# Vertical bars with spikes.
count = 7
for i in range(count):
    x = -W / 2 + 0.06 + i * (W - 0.12) / (count - 1)
    bar(0.012, H - 0.12, (x, 0, (H - 0.12) / 2 + 0.03), steel)
    spike(0.022, 0.09, (x, 0, H + 0.03), accent)
# A diagonal brace for a sturdier look.
bpy.ops.mesh.primitive_cylinder_add(vertices=6, radius=0.01, depth=math.hypot(W - 0.12, H - 0.34), location=(0, 0.02, H / 2 - 0.03))
brace = bpy.context.active_object
brace.rotation_euler = (0, math.atan2(W - 0.12, H - 0.34), 0)
brace.data.materials.append(steel)
parts.append(brace)

bpy.ops.object.select_all(action='DESELECT')
for o in parts:
    o.select_set(True)
bpy.context.view_layer.objects.active = parts[0]
bpy.ops.object.join()
panel = bpy.context.active_object
panel.name = 'CageFencePanel'
bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
bpy.ops.object.shade_flat()

bpy.ops.export_scene.gltf(filepath=out, export_format='GLB', use_selection=True, export_apply=True)
dims = panel.dimensions
print('EXPORTED', out, 'dims_m=%.3f x %.3f x %.3f' % (dims.x, dims.y, dims.z), 'faces=%d' % len(panel.data.polygons))
