# Builds SourceData/VFX/vfx_slice_assets.json from the generated presentation-slice assets (plain Python 3 + numpy):
# per-asset import spec, sha256 of every file and generator, and mechanical checks (frame-border bleed, empty last frame,
# tile seams, loop continuity). Run after the four slice_*.py generators:
#   python SourceData/VFX/Blender/slice_manifest.py SourceData/VFX
import json, os, sys
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import slice_lib as L

ROOT = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
SLICE = os.path.join(ROOT, "Slice")
GENERATORS = ["Blender/slice_lib.py", "Blender/slice_slash.py", "Blender/slice_impact.py", "Blender/slice_water.py", "Blender/slice_shock.py"]
TOOLS = ["Blender/slice_preview.py", "Blender/slice_manifest.py"]

GEOMETRY_SHA = {  # printed by slice_slash.py; the FBX header carries a timestamp, so file bytes differ per export
    "SM_LD_SlashCombo1": "90d481af30e248672fc0757db276816d370fe75f2807776590f8ee727bf1584b",
    "SM_LD_SlashCombo2": "3ec49dec2dac043cd7fc841ef3e567db854440f38007f45c2731177dac4cc3df",
    "SM_LD_SlashCombo3": "a176f56d9cc878bfba71d4c9159358464512c3779bb19836b1700935c366166f",
}

MASK = dict(alpha_packing="RGB = glow mask (R = G = B; 0 ink only, 0.6 element body, 1.0 white-hot core), A = coverage; ink = A > 0 with RGB = 0",
            srgb=False, compression="TC_Default (BC3/DXT5 with alpha) as the existing hit masks; TC_BC7 recommended for thin-line sheets", mips="generate (TMGS_FromTextureGroup)",
            streaming="NeverStream = true, LODGroup Effects (same as VfxApplySettings)")
MATERIAL_HINT = ("Same graph as the existing Layer material: emissive = Glow * tex.rgb (+ white lerp above 0.8) + Ink, opacity = tex.a * Fade, "
                 "translucent unlit, two-sided. Recommended glow remap: color = lerp(Glow * saturate(g / 0.6), 1, smoothstep(0.8, 1.0, g)); Ink (0A0B0D) shows where g = 0 and A > 0.")

FLIP = lambda **kw: kw
ASSETS = [
    dict(id="SM_LD_SlashCombo1", kind="static_mesh", file="Slice/SM_LD_SlashCombo1.fbx", purpose="Basic attack combo stage 1: short flat crescent, swings right to left (head on the left of forward).",
         usage="StaticMeshComponent attached to the character at chest height (or spawned at the muzzle transform), unlit translucent two-sided material using tex_slash_edge; scroll U / dissolve with tex_dissolve_noise over ~0.18 s; rotate with the character facing.",
         pivot="swing origin = chest height = mesh origin (0,0,0)", units="centimetres (scene unit scale 0.01, export apply_unit_scale)", orientation="arc lies in the XY plane (z = 0), character forward = +X, +Y = left of forward",
         uv="UV0: U along the arc 0 = head (leading end) -> 1 = tail; V across 0 = inner edge -> 1 = outer cutting edge (as seen in Unreal; Blender stores 1 - V)",
         extents_cm="x 50..96 (ellipse a = 96, b = 58), width up to 26, sweep 110 degrees", budget="576 triangles, one translucent surface, area about 1.4 m2 equivalent at 26 cm wide (thin crescent)"),
    dict(id="SM_LD_SlashCombo2", kind="static_mesh", file="Slice/SM_LD_SlashCombo2.fbx", purpose="Basic attack combo stage 2: opposite-direction crescent, swings left to right, slightly longer and thinner.",
         usage="As stage 1.", pivot="swing origin = chest height = mesh origin", units="centimetres", orientation="XY plane, forward +X, head on the right of forward",
         uv="as stage 1", extents_cm="x 50..106 (a = 106, b = 66), width up to 30, sweep 118 degrees", budget="576 triangles, one translucent surface"),
    dict(id="SM_LD_SlashCombo3", kind="static_mesh", file="Slice/SM_LD_SlashCombo3.fbx", purpose="Basic attack finisher: wide heavy crescent with a thickened leading edge plus a detached echo blade.",
         usage="As stage 1 but slower (about 0.28 s), with a hold at the thickest point; the echo blade is part of the same mesh and shares the material.",
         pivot="swing origin = chest height = mesh origin", units="centimetres", orientation="XY plane, forward +X, head on the left of forward, sweep wraps about 184 degrees",
         uv="as stage 1 for both ribbons (main + echo)", extents_cm="a = 128, b = 104, main width up to 64 x 1.4 leading-edge bulge, echo width 16", budget="1152 triangles, one translucent surface"),
    dict(id="tex_slash_edge", kind="texture", file="Slice/tex_slash_edge.png", width=512, height=128,
         purpose="Slash ribbon texture for the three combo meshes: white-hot band along the outer cutting edge, element-colour streaks toward the inner edge, wide ink band outside the cutting edge, dark speed streaks, tapering tail.",
         usage="Sampled with the mesh UV0; U (x) is head 0 -> tail 1; scroll or dissolve using tex_dissolve_noise.G streaks. Translucent, unlit.", uv="x = U (head 0 -> tail 1), row 0 = V 0 = inner edge, last row = outer edge", **MASK,
         budget="translucent overdraw: one ribbon surface per swing; 4 players swinging at once ~ 4 x 1.4 m2 equivalent"),
    dict(id="tex_dissolve_noise", kind="texture", file="Slice/tex_dissolve_noise.png", width=256, height=256,
         purpose="Tileable erosion / dissolve utility: R = blobby fBm, G = streaks stretched along texture X (U), B = cellular crackle.", usage="Sample with tiling UV, compare against a progress parameter (opacity = A * step(progress, noise)); G is the along-arc streak dissolve for slashes and jets.",
         uv="tiles in both axes (periodic noise lattice)", alpha_packing="RGB = three independent noise channels in [0,1], A = 1 (utility mask, not a glow mask)", srgb=False, compression="TC_Masks (BC1, no sRGB)", mips="generate", streaming="NeverStream = true",
         budget="0.17 MB with BC1 + mips"),
    dict(id="fb_hit_basic", kind="flipbook", file="Slice/fb_hit_basic.png", width=1024, height=1024, flip=FLIP(cols=4, rows=4, frames=16, frame_size=256, fps=36, duration_s=0.44, loop=False),
         purpose="Directional basic hit: elongated four-point sparkle with a long forward tip, fanned speed lines and thin shards; erodes through the shape.",
         usage="Single quad sprite at the contact point, lying flat or camera-facing, rotated so +X (image right) points along the attack direction; translucent unlit; tint = owner element colour (no element = white, orange only for the local player). 1-2 frame white flash = frames 0-2.",
         pivot="hit point at frame UV (0.30, 0.50); frame spans x -0.6..1.4, y -1..1 in unit space, so one frame = 2 units = suggested 90 cm", uv="frame i at column i % 4, row i / 4 (row 0 on top); +X = hit direction", **MASK, budget="~0.9 m2 quad (90 cm), mean coverage about 7 percent; one quad per hit"),
    dict(id="fb_hit_finisher", kind="flipbook", file="Slice/fb_hit_finisher.png", width=1024, height=1024, flip=FLIP(cols=4, rows=4, frames=16, frame_size=256, fps=30, duration_s=0.53, loop=False),
         purpose="Finisher hit: heavy irregular eight-spike burst with a long forward spike, an expanding forward crescent shock front, chunky debris and long speed lines. Silhouette differs from the basic hit (crescent + chunks), not just scale.",
         usage="As fb_hit_basic; use at about 1.25 x the basic quad size. Basic 3rd-hit only.", pivot="hit point at frame UV (0.30, 0.50)", uv="as fb_hit_basic", **MASK, budget="~1.4 m2 quad (120 cm), mean coverage about 17 percent; one quad per finisher"),
    dict(id="fb_hit_skill", kind="flipbook", file="Slice/fb_hit_skill.png", width=1024, height=1024, flip=FLIP(cols=4, rows=4, frames=16, frame_size=256, fps=28, duration_s=0.57, loop=False),
         purpose="Skill hit: ten-spike comic burst with diagonal broadcast-stripe cut-outs that widen until the burst slices into bars, plus three forward chevrons.",
         usage="As fb_hit_basic; direction-neutral burst with forward chevrons (+X = skill direction). Tint with element colour; use for active skills.", pivot="hit point at frame UV (0.30, 0.50)", uv="as fb_hit_basic", **MASK, budget="~1.4 m2 quad (120 cm), mean coverage about 22 percent early; one quad per skill hit"),
    dict(id="tex_water_jet", kind="texture", file="Slice/tex_water_jet.png", width=512, height=128,
         purpose="Water jet strip for active_hose_down: turbulent-edged body, white-hot centre band and flow streaks, torn-off droplets with ink halos, ink edge on both sides.",
         usage="Ribbon / beam mesh or stretched quad from the caster to the target; U (x) runs along the jet and tiles seamlessly, scroll U at 1.5-3 per second; V (y) is across, centre = 0.5. Translucent, tint = water element colour.",
         uv="tileable along U; V across (row 0 = top edge)", **MASK, budget="one strip surface, ~0.3 m wide x range; 4 simultaneous jets ~ 4 translucent strips"),
    dict(id="fb_water_splash", kind="flipbook", file="Slice/fb_water_splash.png", width=1024, height=1024, flip=FLIP(cols=4, rows=4, frames=16, frame_size=256, fps=30, duration_s=0.53, loop=False),
         purpose="Splash crown at the water impact: fan of curved blades with white-hot veins over the forward half-plane, detached drops, short-lived splat; last frame empty.",
         usage="Sprite at the impact, +X = jet direction; use together with tex_wet_decal on the ground; tint = water colour.", pivot="impact at frame UV (0.30, 0.50)", uv="as fb_hit_basic", **MASK, budget="~0.9 m2 quad, mean coverage about 9 percent"),
    dict(id="tex_wet_decal", kind="texture", file="Slice/tex_wet_decal.png", width=512, height=512,
         purpose="Wet ground puddle mask: organic lobed puddle with run-off tongues, satellite drops, wet-edge shading, white glints and a crisp ink rim.",
         usage="Deferred or mesh decal (or a flat quad just above the ground) at the target; opacity from A times a 1-2 s fade; tint = cool dark water colour; decal size about 160 cm; project along -Z.",
         uv="decal space (U = X, V = Y of the decal projector), puddle centre at (0.5, 0.5)", **MASK, budget="one decal per wet target, 0.5-1 m2, fade 1-2 s"),
    dict(id="tex_shock_arcs", kind="texture", file="Slice/tex_shock_arcs.png", width=512, height=512, variants=8, variant_size=[512, 64],
         purpose="Atlas of 8 lightning strip variants (each 512 x 64, stacked vertically): sharp zig-zag main bolt with 2-4 branches, white-hot core, ink edge. Both ends sit on the strip centre line so chained arcs meet.",
         usage="Ribbon / beam between chain targets for active_zap; pick variant row v (UV V = v/8 .. (v+1)/8), U runs along the arc and can be stretched 2-8x; flicker by swapping the variant every 1-2 frames (about 15 Hz).",
         uv="U along the arc (x), variant row index = floor(V * 8); the bolt crosses the centre line at U = 0 and U = 1", **MASK, budget="one strip per chain link, 1-3 links per cast, translucent additive-or-alpha"),
    dict(id="fb_shock_spark", kind="flipbook", file="Slice/fb_shock_spark.png", width=1024, height=1024, flip=FLIP(cols=4, rows=4, frames=16, frame_size=256, fps=36, duration_s=0.44, loop=False, flicker="a new bolt set every second frame"),
         purpose="Contact spark burst at each shocked target: seven forked angular bolts radiating from a four-point flash, detached zig-zag fragments, bolts truncate and break up; last frame is sparse specks.",
         usage="Camera-facing or flat sprite at the contact point, omnidirectional; tint = shock colour (white core remains).", pivot="centre of frame (0.5, 0.5); frame spans -1..1, suggested 100 cm", uv="frame i at column i % 4, row i / 4", **MASK, budget="~1.0 m2 quad, mean coverage about 5 percent"),
    dict(id="fb_shock_field", kind="flipbook", file="Slice/fb_shock_field.png", width=1024, height=1024, flip=FLIP(cols=4, rows=4, frames=16, frame_size=256, fps=16, duration_s=1.0, loop=True, loop_note="frame 15 -> 0 is seamless: hatch scrolls exactly one period, crackle flicker repeats every 4 frames, bolt sets every 2 frames over 8 sets"),
         purpose="Electric ground field for electrified targets and the conduct reaction: domain-warped crackle net, diagonal court hatch, three bolts that jump across the patch, dashed chalk boundary. Readable from above; not a glow blob.",
         usage="Decal (or flat quad) under the affected area, looped while the field lives; opacity fades in/out at runtime (no fade baked in); size = patch about 150 percent of the gameplay radius (patch fills 0.74 of the half frame); tint = shock colour.",
         pivot="centre (0.5, 0.5)", uv="frame i at column i % 4, row i / 4; looping", **MASK, budget="one decal per electrified area; mean coverage about 11 percent; sample 2 frames at 16 fps or a single frame via Flipbook node"),
]


def mem(width, height):
    rgba = width * height * 4
    return dict(uncompressed_rgba8_bytes=int(rgba * 4 / 3), bc3_bc7_with_mips_bytes=int(width * height * 4 / 3))


def checks(asset, img):
    out = {}
    if asset["kind"] == "flipbook":
        fs = asset["flip"]["frame_size"]
        cols, rows = asset["flip"]["cols"], asset["flip"]["rows"]
        border, cover = 0.0, []
        for i in range(cols * rows):
            r, c = divmod(i, cols)
            fr = img[r * fs:(r + 1) * fs, c * fs:(c + 1) * fs, 3]
            ring = np.concatenate([fr[:2].ravel(), fr[-2:].ravel(), fr[:, :2].ravel(), fr[:, -2:].ravel()])
            border = max(border, float(ring.max()))
            cover.append(float(fr.mean()))
        out["max_alpha_on_2px_frame_border"] = round(border, 4)
        out["mean_coverage_per_frame"] = [round(x, 4) for x in cover]
        out["last_frame_mean_coverage"] = round(cover[-1], 5)
        out["mean_coverage_peak"] = round(max(cover), 4)
        out["mean_coverage_average"] = round(float(np.mean(cover)), 4)
        if asset["flip"].get("loop"):
            def frame(i):
                r, c = divmod(i, cols)
                return img[r * fs:(r + 1) * fs, c * fs:(c + 1) * fs, 3]
            out["loop_seam_mean_abs_alpha_diff_15_to_0"] = round(float(np.abs(frame(15) - frame(0)).mean()), 5)
            out["adjacent_frame_mean_abs_alpha_diff"] = round(float(np.mean([np.abs(frame(i) - frame(i + 1)).mean() for i in range(15)])), 5)
    if asset["id"] == "tex_water_jet":
        out["seam_mean_abs_diff_left_right_columns"] = round(float(np.abs(img[:, 0] - img[:, -1]).mean()), 5)
        out["adjacent_column_mean_abs_diff"] = round(float(np.abs(img[:, 100] - img[:, 101]).mean()), 5)
    if asset["id"] == "tex_dissolve_noise":
        out["seam_mean_abs_diff_x"] = round(float(np.abs(img[:, 0, :3] - img[:, -1, :3]).mean()), 5)
        out["seam_mean_abs_diff_y"] = round(float(np.abs(img[0, :, :3] - img[-1, :, :3]).mean()), 5)
        out["adjacent_pixel_mean_abs_diff"] = round(float(np.abs(img[:, 100, :3] - img[:, 101, :3]).mean()), 5)
    return out


def main():
    entries = []
    for asset in ASSETS:
        e = dict(asset)
        path = os.path.join(ROOT, asset["file"])
        e["sha256"] = L.sha256_file(path)
        if asset["kind"] == "static_mesh":
            e["geometry_sha256"] = GEOMETRY_SHA[asset["id"]]
            e["sha256_note"] = "sha256 is the committed FBX bytes; regenerating changes it (FBX creation timestamp), geometry_sha256 is the reproducible check"
            e["vertex_count"] = 365 if asset["id"] != "SM_LD_SlashCombo3" else 730
            e["triangle_count"] = 576 if asset["id"] != "SM_LD_SlashCombo3" else 1152
            e["fbx_export"] = "Blender 5.2.4 export_scene.fbx: apply_unit_scale, scale_length 0.01, axis_forward X, axis_up Z, mesh_smooth_type FACE, no leaf bones; verify orientation after import (see HANDOFF)"
        else:
            img = L.read_png(path)
            e["width"], e["height"] = img.shape[1], img.shape[0]
            e.update(mem(e["width"], e["height"]))
            e["checks"] = checks(asset, img)
        e["material_hint"] = MATERIAL_HINT if asset["kind"] in ("texture", "flipbook") and asset["id"] != "tex_dissolve_noise" else None
        if e["material_hint"] is None:
            del e["material_hint"]
        entries.append(e)
    doc = dict(schema=1, provenance="Generated for this project by SourceData/VFX/Blender/slice_*.py in Blender 5.2.4 headless (bpy mesh + FBX export; numpy 2.x raster and noise with fixed seeds); no third-party material.",
               generators={g: L.sha256_file(os.path.join(ROOT, g)) for g in GENERATORS}, tools={t: L.sha256_file(os.path.join(ROOT, t)) for t in TOOLS},
               glow_levels=dict(ink=0.0, body=L.GLOW_BODY, core=L.GLOW_CORE),
               ground_swatches_rgb=dict(dark_vfx_capture=[22, 26, 24], shaded_paving=[116, 121, 126], sunlit_warm_paving=[189, 181, 166]),
               assets=entries)
    with open(os.path.join(ROOT, "vfx_slice_assets.json"), "w", newline="\n") as handle:
        json.dump(doc, handle, indent=2)
        handle.write("\n")
    print("wrote", os.path.join(ROOT, "vfx_slice_assets.json"), len(entries), "assets")
    for e in entries:
        if "checks" in e:
            print(e["id"], {k: v for k, v in e["checks"].items() if "per_frame" not in k})


main()
