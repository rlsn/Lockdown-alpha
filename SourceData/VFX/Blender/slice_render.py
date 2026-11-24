# Vector-shape renderer for the presentation-slice VFX sources (runs inside Blender: bpy + numpy).
# Shapes are real Blender geometry rendered orthographically by Workbench, then read back as the glow + coverage pack:
#   * strokes  = poly curves with a round bevel; the per-point radius gives the tapered, varying-width profile
#   * fills    = closed flat curves (filled); an optional bevel grows the silhouette for an ink rim
# Each shape is drawn as stacked copies: the ink copy (widest, glow 0) at the bottom, the element body (glow 0.6) above it
# and the white-hot core (glow 1.0) on top. Workbench flat lighting writes each object's colour unchanged, so the render's
# RGB is the glow mask and its alpha the anti-aliased coverage: exactly the RGB-glow + A packing of slice_lib.
# Output is read from a linear OpenEXR render (no view transform), straight alpha, row 0 on top.
import math, os, tempfile
import numpy as np
import bpy

INK, BODY, CORE = 0.0, 0.6, 1.0
_LAYER_Z = {INK: 0.0, BODY: 100.0, CORE: 200.0}  # far apart: a fat ink tube never pokes above a body


class Canvas:
    """An orthographic Workbench canvas covering x0..x1, y0..y1 (shape units, y up) at width x height pixels."""

    def __init__(self, width, height, x0, x1, y0, y1, samples="32"):
        bpy.ops.wm.read_factory_settings(use_empty=True)
        scene = bpy.context.scene
        scene.render.engine = "BLENDER_WORKBENCH"
        scene.render.resolution_x, scene.render.resolution_y = width, height
        scene.render.resolution_percentage = 100
        scene.render.film_transparent = True
        scene.render.pixel_aspect_x = scene.render.pixel_aspect_y = 1.0
        scene.display.render_aa = samples
        shading = scene.display.shading
        shading.light = "FLAT"
        shading.color_type = "OBJECT"
        shading.show_object_outline = False
        shading.show_cavity = False
        shading.show_shadows = False
        shading.show_specular_highlight = False
        scene.view_settings.view_transform = "Standard"
        scene.view_settings.look = "None"
        scene.render.image_settings.file_format = "OPEN_EXR"
        scene.render.image_settings.color_depth = "32"
        scene.render.image_settings.color_mode = "RGBA"
        cam = bpy.data.cameras.new("Ortho")
        cam.type = "ORTHO"
        cam.sensor_fit = "HORIZONTAL" if (x1 - x0) / width >= (y1 - y0) / height else "VERTICAL"
        cam.ortho_scale = max(x1 - x0, (y1 - y0) * width / height) if cam.sensor_fit == "HORIZONTAL" else (y1 - y0)
        cam.clip_start, cam.clip_end = 0.1, 1000.0
        rig = bpy.data.objects.new("Ortho", cam)
        rig.location = ((x0 + x1) / 2.0, (y0 + y1) / 2.0, 500.0)
        scene.collection.objects.link(rig)
        scene.camera = rig
        self.scene, self.width, self.height = scene, width, height
        self.count = 0

    def _object(self, curve, glow):
        self.count += 1
        obj = bpy.data.objects.new(f"S{self.count:04d}", curve)
        obj.color = (glow, glow, glow, 1.0)
        self.scene.collection.objects.link(obj)
        return obj

    def stroke(self, pts, radii, glow, order=0.0):
        """Open poly stroke through pts with a per-point half-width (radii). Corner radii are widened by 1 / cos(turn / 2)
        so a sharp kink keeps its full width instead of pinching where the round bevel turns."""
        curve = bpy.data.curves.new(f"C{self.count:04d}", "CURVE")
        curve.dimensions = "3D"
        curve.bevel_depth = 1.0
        curve.bevel_resolution = 4
        curve.use_fill_caps = True
        curve.twist_mode = "Z_UP"
        spline = curve.splines.new("POLY")
        spline.points.add(len(pts) - 1)
        z = _LAYER_Z[glow] + order
        for i, ((x, y), r) in enumerate(zip(pts, radii)):
            k = 1.0
            if 0 < i < len(pts) - 1:
                ax, ay = x - pts[i - 1][0], y - pts[i - 1][1]
                bx, by = pts[i + 1][0] - x, pts[i + 1][1] - y
                la, lb = math.hypot(ax, ay), math.hypot(bx, by)
                if la > 1e-9 and lb > 1e-9:
                    turn = math.acos(max(-1.0, min(1.0, (ax * bx + ay * by) / (la * lb))))
                    k = min(1.0 / max(math.cos(turn / 2.0), 1e-3), 2.2)
            spline.points[i].co = (x, y, z, 1.0)
            spline.points[i].radius = max(r * k, 0.0)
        return self._object(curve, glow)

    def fill(self, pts, glow, grow=0.0, order=0.0):
        """Closed filled polygon; grow > 0 widens the silhouette by that distance (round-bevelled rim), e.g. for ink."""
        curve = bpy.data.curves.new(f"F{self.count:04d}", "CURVE")
        curve.dimensions = "2D"
        curve.fill_mode = "BOTH"
        curve.bevel_depth = grow
        curve.bevel_resolution = 4 if grow > 0 else 0
        spline = curve.splines.new("POLY")
        spline.points.add(len(pts) - 1)
        for i, (x, y) in enumerate(pts):
            spline.points[i].co = (x, y, 0.0, 1.0)
        spline.use_cyclic_u = True
        obj = self._object(curve, glow)
        obj.location.z = _LAYER_Z[glow] + order
        return obj

    def layered_stroke(self, pts, radii, ink, core_ratio, order=0.0):
        """One stroke as ink rim (radius + ink) under the element body under the white-hot core (radius * core_ratio)."""
        self.stroke(pts, [r + ink * min(1.0, r / max(ink, 1e-6) * 2.0) for r in radii], INK, order)
        self.stroke(pts, radii, BODY, order)
        if core_ratio > 0:
            self.stroke(pts, [r * core_ratio for r in radii], CORE, order)

    def render(self):
        """Renders and returns float32 RGBA (height, width, 4): RGB = glow, A = coverage, row 0 on top, straight alpha."""
        path = os.path.join(tempfile.gettempdir(), f"ld_slice_render_{os.getpid()}.exr")
        self.scene.render.filepath = path
        bpy.ops.render.render(write_still=True)
        image = bpy.data.images.load(path)
        image.colorspace_settings.name = "Non-Color"
        buf = np.empty(self.width * self.height * 4, dtype=np.float32)
        image.pixels.foreach_get(buf)
        bpy.data.images.remove(image)
        os.remove(path)
        rgba = buf.reshape(self.height, self.width, 4)[::-1].copy()
        alpha = rgba[..., 3:4]
        # Workbench EXRs hold premultiplied colour: divide it out so RGB is the glow wherever there is coverage.
        rgba[..., :3] = np.where(alpha > 1e-4, rgba[..., :3] / np.maximum(alpha, 1e-4), 0.0)
        rgba = np.clip(rgba, 0.0, 1.0)
        grey = rgba[..., :3].mean(axis=2, keepdims=True)
        return np.concatenate([grey, grey, grey, alpha], axis=2)
