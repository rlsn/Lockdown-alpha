"""Extract the production basketball bank from the project's retargeted pose and polish exports.

Inputs are the existing BasketballPolish poses_original.json and PackageMotion.json records.
No retargeting, new motion synthesis, gameplay timing or machine paths are stored in the result.
"""
import argparse
import hashlib
import json
from pathlib import Path

GROUPS = {
    "idle": [11, 12], "run": [7], "handling": [43, 44],
    "passes": [78, 79], "standing_shots": [148, 149, 152, 153],
    "moving_shots": [156, 157], "turns": [98, 99], "crossovers": [41],
}


def build(poses_path, polish_path):
    poses = json.loads(poses_path.read_text(encoding="utf-8-sig"))
    polish = json.loads(polish_path.read_text(encoding="utf-8-sig"))
    originals = {row["id"]: row for row in poses["clips"]}
    calibrated = {row["id"]: row for row in polish["clips"]}
    out = {
        "schema": 1, "fps": 30,
        "provenance": "Existing project basketball library, retargeted to the Tripo hooper rig; final palm, gaze, ball and spin corrections",
        "pose_export_sha256": hashlib.sha256(poses_path.read_bytes()).hexdigest(),
        "polish_export_sha256": hashlib.sha256(polish_path.read_bytes()).hexdigest(),
        # Native planted-foot measurement after the production body's height/forward transform.
        "run_speed_cm_per_sec": 426.8, "ball_radius_cm": 12,
        "palms": polish["sockets"], "palm_normals": polish["normal_local"],
        "groups": GROUPS, "clips": [],
    }
    for ident in sorted({i for ids in GROUPS.values() for i in ids}):
        raw, fix = originals[ident], calibrated[ident]
        frames = raw["frames"]
        assert len(frames) == round(raw["length"] * 30) + 1
        tracks = {}
        for bone in poses["names"]:
            channels = {}
            for key in ("p", "q", "s"):
                values = fix["tracks"].get(bone, {}).get(key)
                if values is None:
                    values = [frame["local"][bone][key] for frame in frames]
                assert len(values) == len(frames)
                values = [[round(float(v), 7) for v in value] for value in values]
                if all(value == values[0] for value in values):
                    values = values[:1]
                channels[key] = values
            tracks[bone] = channels
        curves = {"BallX": [v[0] for v in fix["ball"]], "BallY": [v[1] for v in fix["ball"]],
                  "BallZ": [v[2] for v in fix["ball"]], "BallLeft": fix["left"], "BallContact": fix["contact"]}
        if "turn_facing" in fix:
            curves["TurnFacing"] = fix["turn_facing"]
        assert all(len(values) == len(frames) for values in curves.values())
        origin = fix["path"].split("/")[-1].split(".")[0]
        alias = {7: "Hooper_Run", 11: "Hooper_Idle", 78: "Hooper_Attack", 148: "Hooper_Cast"}
        out["clips"].append({
            "id": ident, "asset": alias.get(ident, origin), "origin_asset": origin,
            "frames": len(frames), "start": fix["start"], "release": fix["release"], "end": fix["end"],
            "left_hand": fix["left_hand"], "tracks": tracks,
            "curves": {key: [round(float(v), 6) for v in values] for key, values in curves.items()},
        })
    return out


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--poses", type=Path, required=True)
    parser.add_argument("--polish", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    result = build(args.poses, args.polish)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, separators=(",", ":"), ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"Extracted {len(result['clips'])} calibrated clips: {args.out} ({args.out.stat().st_size} bytes)")
