"""Measure the CC0 character sources listed in SourceData/Art/art_assets.json.

The manifest keeps the numbers the native importer uses (bind height, run reference speed, attack/cast impact
times). This script derives them from the glTF data itself so they are reproducible, and --check fails when the
manifest disagrees with the sources. It reads glTF 2.0 JSON files with embedded or external buffers; no engine needed.
"""

import argparse
import base64
import json
import math
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
ART = ROOT / "SourceData" / "Art"
MANIFEST = ART / "art_assets.json"
SAMPLE_HZ = 240


def load_gltf(path: Path):
    data = json.loads(path.read_text(encoding="utf-8"))
    buffers = []
    for buffer in data["buffers"]:
        uri = buffer["uri"]
        if uri.startswith("data:"):
            buffers.append(base64.b64decode(uri.split(",", 1)[1]))
        else:
            buffers.append((path.parent / uri).read_bytes())
    return data, buffers


def accessor(data, buffers, index):
    acc = data["accessors"][index]
    view = data["bufferViews"][acc["bufferView"]]
    width = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}[acc["type"]]
    dtype = {5126: np.float32, 5123: np.uint16, 5125: np.uint32, 5121: np.uint8}[acc["componentType"]]
    start = view.get("byteOffset", 0) + acc.get("byteOffset", 0)
    stride = view.get("byteStride", 0)
    item = np.dtype(dtype).itemsize * width
    raw = buffers[view["buffer"]]
    if stride and stride != item:
        rows = [np.frombuffer(raw, dtype, width, start + i * stride) for i in range(acc["count"])]
        return np.array(rows)
    return np.frombuffer(raw, dtype, acc["count"] * width, start).reshape(acc["count"], width).astype(np.float64)


def quat_matrix(q):
    x, y, z, w = q
    return np.array([
        [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
        [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
        [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)],
    ])


def slerp(a, b, t):
    dot = float(np.dot(a, b))
    if dot < 0:
        b, dot = -b, -dot
    if dot > 0.9995:
        out = a + t * (b - a)
        return out / np.linalg.norm(out)
    theta = math.acos(dot)
    return (math.sin((1 - t) * theta) * a + math.sin(t * theta) * b) / math.sin(theta)


class Rig:
    def __init__(self, path: Path):
        self.data, self.buffers = load_gltf(path)
        nodes = self.data["nodes"]
        self.names = [node.get("name", str(i)) for i, node in enumerate(nodes)]
        self.parent = {}
        for i, node in enumerate(nodes):
            for child in node.get("children", []):
                self.parent[child] = i
        self.rest = []
        for node in nodes:
            self.rest.append((np.array(node.get("translation", [0, 0, 0]), float),
                              np.array(node.get("rotation", [0, 0, 0, 1]), float),
                              np.array(node.get("scale", [1, 1, 1]), float)))
        self.clips = {anim["name"]: anim for anim in self.data.get("animations", [])}

    def node(self, name):
        # The manifest names bones as the UE skeleton does; Interchange turns the '.' of glTF node names into '_'.
        if name in self.names:
            return self.names.index(name)
        return self.names.index(name.replace("_", "."))

    def duration(self, clip):
        anim = self.clips[clip]
        return max(float(accessor(self.data, self.buffers, s["input"]).max()) for s in anim["samplers"])

    def pose(self, clip, time):
        pose = [list(trs) for trs in self.rest]
        anim = self.clips[clip]
        for channel in anim["channels"]:
            sampler = anim["samplers"][channel["sampler"]]
            times = accessor(self.data, self.buffers, sampler["input"])[:, 0]
            values = accessor(self.data, self.buffers, sampler["output"])
            path = channel["target"]["path"]
            if path not in ("translation", "rotation", "scale"):
                continue
            slot = {"translation": 0, "rotation": 1, "scale": 2}[path]
            if time <= times[0]:
                value = values[0]
            elif time >= times[-1]:
                value = values[-1]
            else:
                k = int(np.searchsorted(times, time)) - 1
                t = (time - times[k]) / (times[k + 1] - times[k])
                if sampler.get("interpolation", "LINEAR") == "STEP":
                    value = values[k]
                elif slot == 1:
                    value = slerp(values[k], values[k + 1], t)
                else:
                    value = values[k] + t * (values[k + 1] - values[k])
            pose[channel["target"]["node"]][slot] = np.array(value, float)
        return pose

    def world(self, pose, index):
        matrix = np.eye(4)
        while index is not None:
            t, r, s = pose[index]
            local = np.eye(4)
            local[:3, :3] = quat_matrix(r) * s
            local[:3, 3] = t
            matrix = local @ matrix
            index = self.parent.get(index)
        return matrix

    def position(self, clip, time, name):
        return self.world(self.pose(clip, time), self.node(name))[:3, 3]

    def bind_height(self):
        lows, highs = [], []
        for mesh in self.data["meshes"]:
            for primitive in mesh["primitives"]:
                acc = self.data["accessors"][primitive["attributes"]["POSITION"]]
                lows.append(acc["min"][1])
                highs.append(acc["max"][1])
        return max(highs) - min(lows)


def measure(path: Path, clips: dict, hand: str, foot: str):
    rig = Rig(path)
    out = {"bind_height_m": round(rig.bind_height(), 4), "clips": {}}
    for state, clip in clips.items():
        out["clips"][state] = {"clip": clip, "duration_sec": round(rig.duration(clip), 4)}
    # Forward is glTF +Z: the impact is the frame where the hand reaches furthest forward of the hips.
    for state in ("Attack", "Cast"):
        clip = clips[state]
        duration = rig.duration(clip)
        times = np.linspace(0, duration, int(duration * SAMPLE_HZ) + 1)
        reach = [rig.position(clip, t, hand)[2] - rig.position(clip, t, "Hips")[2] for t in times]
        out["clips"][state]["impact_sec"] = round(float(times[int(np.argmax(reach))]), 3)
    # In-place run: while the foot is planted it slides backwards at the body's ground speed.
    clip = clips["Run"]
    duration = rig.duration(clip)
    times = np.linspace(0, duration, int(duration * SAMPLE_HZ) + 1)
    positions = np.array([rig.position(clip, t, foot) for t in times])
    height = positions[:, 1]
    planted = height <= height.min() + 0.02
    speeds = -np.gradient(positions[:, 2], times)[planted]
    out["run_speed_m_per_sec"] = round(float(np.median(speeds)), 3)
    out["death_end_offset_m"] = round(float(rig.position(clips["Death"], rig.duration(clips["Death"]), "Hips")[2]
                                            - rig.position(clips["Death"], 0.0, "Hips")[2]), 3)
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="fail when the manifest disagrees with the measured sources")
    args = parser.parse_args()
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    failures = []
    report = {}
    for character in manifest["characters"]:
        clips = {anim["state"]: anim["clip"] for anim in character["animations"]}
        measured = measure(ART / character["source"], clips, character["hand_bone"], character["foot_bone"])
        scale = character["height_cm"] / (measured["bind_height_m"] * 100.0)
        measured["run_speed_cm_per_sec"] = round(measured["run_speed_m_per_sec"] * 100.0 * scale, 1)
        report[character["id"]] = measured
        if args.check:
            if abs(character["run_speed_cm_per_sec"] - measured["run_speed_cm_per_sec"]) > 0.5:
                failures.append(f"{character['id']} run_speed_cm_per_sec {character['run_speed_cm_per_sec']} != {measured['run_speed_cm_per_sec']}")
            for anim in character["animations"]:
                expected = measured["clips"][anim["state"]].get("impact_sec")
                if expected is not None and abs(anim.get("impact_sec", -1) - expected) > 0.0015:
                    failures.append(f"{character['id']} {anim['state']} impact_sec {anim.get('impact_sec')} != {expected}")
    print(json.dumps(report, indent=2))
    for failure in failures:
        print("MISMATCH", failure, file=sys.stderr)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
