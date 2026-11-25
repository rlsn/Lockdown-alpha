# Builds SourceData/VFX/vfx_slice_assets.json from the generated presentation-slice assets (plain Python 3 + numpy):
# per-asset import spec, sha256 of every file and generator, and mechanical checks (frame-border bleed, empty last frame,
# tile seams, loop continuity). Run after the five slice_*.py generators:
#   python SourceData/VFX/Blender/slice_manifest.py SourceData/VFX
import json, os, sys
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import slice_lib as L

ROOT = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
SLICE = os.path.join(ROOT, "Slice")
GENERATORS = ["Blender/slice_lib.py", "Blender/slice_slash.py", "Blender/slice_impact.py", "Blender/slice_render.py",
              "Blender/slice_chain_candidates.py", "Blender/slice_field.py", "Blender/slice_elements.py"]
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
    dict(id="fb_hit_skill", kind="flipbook", file="Slice/fb_hit_skill.png", width=1024, height=1024, flip=FLIP(cols=4, rows=4, frames=16, frame_size=256, fps=28, duration_s=0.57, loop=False),
         purpose="Skill hit: ten-spike comic burst with diagonal broadcast-stripe cut-outs that widen until the burst slices into bars, plus three forward chevrons.",
         usage="Single quad sprite at the contact point, +X along the attack direction, hit point at frame UV (0.30, 0.50); direction-neutral burst with forward chevrons (+X = skill direction). Tint with element colour; use for active skills.", pivot="hit point at frame UV (0.30, 0.50)", uv="as fb_hit_basic", **MASK, budget="~1.4 m2 quad (120 cm), mean coverage about 22 percent early; one quad per skill hit"),
    # ---- chain-lightning art-direction candidates (vfx-direction/CANDIDATES.md); element assets only, trunk shapes come from Niagara points
    dict(id="chainA_profile", kind="texture", file="Slice/chainA_profile.png", width=256, height=64, candidate="A",
         purpose="Candidate A cross-section strip: warm-white core (about 25 % of the width) inside a soft energy layer that falls off to transparent, over a wider dim amber under-layer (glow about 0.3). No outline.",
         usage="Niagara ribbon / beam material: U = world length / tile length (tiles, constant density per cm), V across the trunk; core additive, under-layer alpha. Multiply by chainA_energy_noise scrolling along U.",
         uv="seamless along U; V across, centre 0.5", **MASK, budget="one ribbon per hop plus 1-3 thin branch ribbons"),
    dict(id="chainA_energy_noise", kind="texture", file="Slice/chainA_energy_noise.png", width=256, height=256, candidate="A",
         purpose="Candidate A tileable flowing energy noise: R domain-warped flow, G ridged filaments stretched along U, B a second filament phase.",
         usage="Scroll along the ribbon U (tiled by world length) and multiply the trunk opacity, or cross-fade G and B for flow.",
         uv="tiles in both axes", alpha_packing="utility noise: R flow, G filaments, B second filament phase, A = 1 (not a glow mask)", srgb=False,
         compression="TC_Masks (no sRGB)", mips="generate", streaming="NeverStream = true", budget="shared by all A hops"),
    dict(id="chainA_spark_streak", kind="texture", file="Slice/chainA_spark_streak.png", width=128, height=32, candidate="A",
         purpose="Candidate A velocity-stretched spark: +X = velocity, hot round head at U 0.83, thin tail fading to the left, soft halo.",
         usage="Velocity-aligned sprite for 6-10 hit sparks (0.15-0.25 s, slight gravity), stretch length by speed.", pivot="head at frame UV (0.83, 0.5)",
         uv="+X = velocity", **MASK, budget="6-10 sprites per hit"),
    dict(id="chainA_flash", kind="texture", file="Slice/chainA_flash.png", width=128, height=128, candidate="A",
         purpose="Candidate A small soft hit flash: warm-white soft core with faint anamorphic streaks (longer along X); no spikes, no outline.",
         usage="Sprite at the contact point for 2-3 frames; core diameter about 0.32 of the quad, so a 70 cm quad keeps the flash under half the body width.", pivot="centre",
         uv="+X = long streak axis", **MASK, budget="one sprite per hit"),
    dict(id="chainA_ground_glow", kind="texture", file="Slice/chainA_ground_glow.png", width=256, height=256, candidate="A",
         purpose="Candidate A soft ground-glow mask under a hit (stands in for a small shadowless point light): smooth falloff, gently uneven rim, hotter centre.",
         usage="Additive flat quad or decal under the hit body for about 0.15 s; about 110 cm.", pivot="centre", uv="decal space", **MASK, budget="one per hit"),
    dict(id="chainB_scorch_dash", kind="texture", file="Slice/chainB_scorch_dash.png", width=256, height=128, candidate="B",
         purpose="Candidate B angular scorch dash decal: tapered slash with two side chips, burnt ragged edge and soot feather (glow 0 = scorch), short ember seam inside (glow 0.6).",
         usage="Ground decal under the target, X along the hop, about 130 cm, fades over 0.6 s; tint the ink a warm dark (not black).", pivot="dash starts at U 0.05",
         uv="x = hop direction", **MASK, budget="one decal per hit"),
    # ---- electric field charged floor (field redesign candidate F3); slice_field.py
    dict(id="field_web", kind="texture", file="Slice/field_web.png", width=512, height=512,
         purpose="Tileable isotropic branching electric crack web: jittered points joined to their neighbours by kinked strokes with short forks, warm-white core inside an element body and a soft halo. No outline.",
         usage="Charged-floor layer of the electric field: sampled in world space (about 320 cm per tile) inside the field's radius, low opacity, re-randomised by UV jumps a few times a second; cells flare with field_energy_noise B.",
         uv="tiles in both axes", **MASK, budget="shared by every field; one floor disc per field"),
    dict(id="field_energy_noise", kind="texture", file="Slice/field_energy_noise.png", width=256, height=256,
         purpose="Tileable utility noise of the charged floor: R soft energy blobs, G a second blob phase, B the field_web cell id (one random value per web point's cell, aligned with field_web at the same UV).",
         usage="R/G mask where the web shows (slow drift); B picks the cells that flare briefly.",
         uv="tiles in both axes, same tile as field_web", alpha_packing="utility noise: R, G soft blobs, B web cell id, A = 1 (not a glow mask)", srgb=False,
         compression="TC_Masks (no sRGB)", mips="generate", streaming="NeverStream = true", budget="shared by every field"),
    # ---- the other elements' surfaces (slice_elements.py); each element its own, no outline
    dict(id="water_flow", kind="texture", file="Slice/water_flow.png", width=256, height=128,
         purpose="A water stream's surface: a see-through body thicker towards its ragged soft sides, long flow streaks and sparse cool glints; no outline.",
         usage="Niagara ribbon of active_hose_down's stream and the floor sheet of its windup band: U along the flow (tiled by world length), V across; also its own scrolling noise (G) so the surface flows.",
         uv="tileable along U; V across, centre 0.5", **MASK, budget="one ribbon per stream, one floor sprite per windup"),
    dict(id="water_drop", kind="texture", file="Slice/water_drop.png", width=64, height=64,
         purpose="One droplet: a translucent bead with a thicker rim and an upper-left highlight.",
         usage="Velocity-aligned sprites of beads, crowns and drips (5-15 cm), stretched a little by speed.", pivot="centre", uv="centred", **MASK, budget="up to about 70 sprites per own stream"),
    dict(id="water_wet", kind="texture", file="Slice/water_wet.png", width=256, height=256,
         purpose="A soaked floor: dark irregular damp blotches (glow 0 darkens the floor) with rare small glints.",
         usage="Floor-laid sprites under a stream's band and a struck body, fading over about a second.", uv="tiles in both axes", **MASK, budget="six patches per stream, one per water hit"),
    dict(id="soft_puff", kind="texture", file="Slice/soft_puff.png", width=128, height=128,
         purpose="A soft lumpy cloud puff, uniform glow 0.3 (between a palette's edge and body), fading to a clear border.",
         usage="Camera-facing sprites of mist, steam, dust and smoke, tinted by their palette.", pivot="centre", uv="centred", **MASK, budget="a few per effect"),

    dict(id="fire_tongue", kind="texture", file="Slice/fire_tongue.png", width=256, height=512, atlas=dict(cols=2, rows=2, cell_width=128, cell_height=256),
         purpose="Four flame tongues (2 x 2 atlas, base at the bottom of each cell): soft sides, rising streaks, a soft hot core low in the middle and a licking tip that thins into wisps; no outline, no flat fill.",
         usage="Camera-facing sprites of fire effects with the pivot at the base (U 0.5, V 1.0 of the cell), one random cell each; rising and shrinking over their life.",
         pivot="base at cell UV (0.5, 1.0)", uv="2 x 2 sub-images", **MASK, budget="tens per fire effect"),
    dict(id="fire_char", kind="texture", file="Slice/fire_char.png", width=256, height=256,
         purpose="A scorched floor: soft soot blotches (glow 0 darkens) crossed by thin glowing ember cracks along cell borders.",
         usage="Floor-laid sprites under fire areas and burnt ground, the cracks flickering by intensity.", uv="tiles in both axes", **MASK, budget="one or a few per fire area"),
    dict(id="ice_shard", kind="texture", file="Slice/ice_shard.png", width=256, height=256, atlas=dict(cols=2, rows=2, cell_width=128, cell_height=128),
         purpose="Four ice crystals (2 x 2 atlas): faceted see-through shards, each facet its own brightness, bright ridges and a glint; no outline.",
         usage="Sprites of chill effects (shards bursting, growing or shattering), one random cell each, tip up.", pivot="centre", uv="2 x 2 sub-images", **MASK, budget="tens per chill effect"),
    dict(id="ice_frost", kind="texture", file="Slice/ice_frost.png", width=256, height=256,
         purpose="A frosted floor: a pale sheen crossed by thin branching frost needles at three scales.",
         usage="Floor-laid sprites of ice ground and the chill cone's frosted floor.", uv="tiles in both axes", **MASK, budget="one or a few per chill area"),
    dict(id="rot_murk", kind="texture", file="Slice/rot_murk.png", width=256, height=256,
         purpose="A rotten murk: soft lumpy clouds, darker cores and scattered thin bubble rims.",
         usage="Floor-laid and low camera-facing sprites of miasma and rot effects, slowly drifting.", uv="tiles in both axes", **MASK, budget="a few per rot area"),
    dict(id="rot_bubble", kind="texture", file="Slice/rot_bubble.png", width=64, height=64,
         purpose="One bubble: a thin rim with a highlight and a nearly clear middle.",
         usage="Sprites of bubbles rising and popping over rot effects.", pivot="centre", uv="centred", **MASK, budget="tens per rot area"),
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
    if asset["id"] == "water_flow":
        out["seam_mean_abs_diff_left_right_columns"] = round(float(np.abs(img[:, 0] - img[:, -1]).mean()), 5)
        out["adjacent_column_mean_abs_diff"] = round(float(np.abs(img[:, 100] - img[:, 101]).mean()), 5)
    if asset["id"].startswith("chain"):
        if asset["id"] in ("chainA_profile", "chainA_energy_noise"):
            out["seam_mean_abs_diff_left_right_columns"] = round(float(np.abs(img[:, 0] - img[:, -1]).mean()), 5)
            out["adjacent_column_mean_abs_diff"] = round(float(np.abs(img[:, 100] - img[:, 101]).mean()), 5)
        else:
            a = img[..., 3]
            cell = asset.get("atlas", {}).get("cell")
            tiles = [a[r:r + cell, c:c + cell] for r in range(0, a.shape[0], cell) for c in range(0, a.shape[1], cell)] if cell else [a]
            out["max_alpha_on_2px_border"] = round(max(float(np.concatenate([t[[0, 1, -2, -1]].ravel(), t[:, [0, 1, -2, -1]].ravel()]).max()) for t in tiles), 4)
        out["mean_coverage"] = round(float(img[..., 3].mean()), 4)
    if asset["id"].startswith("field_"):
        out["seam_mean_abs_diff_x"] = round(float(np.abs(img[:, 0] - img[:, -1]).mean()), 5)
        out["seam_mean_abs_diff_y"] = round(float(np.abs(img[0] - img[-1]).mean()), 5)
        out["adjacent_pixel_mean_abs_diff"] = round(float(np.abs(img[:, 100] - img[:, 101]).mean()), 5)
        out["mean_coverage"] = round(float(img[..., 3].mean()), 4)
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
    doc = dict(schema=1, provenance="Generated for this project by SourceData/VFX/Blender/slice_*.py in Blender 5.2.4 headless (bpy mesh + FBX export; Workbench renders of curve geometry via slice_render.py; chain* entries are art-direction candidate elements; numpy 2.x raster and noise with fixed seeds); no third-party material.",
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
