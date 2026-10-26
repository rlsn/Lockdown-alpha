"""Independently verify native B86 reports and recompute recorded percentiles."""
import json
import math
import sys
from pathlib import Path


def read(path):
    raw = Path(path).read_bytes()
    return json.loads(raw.decode("utf-16" if raw[:2] in (b"\xff\xfe", b"\xfe\xff") else "utf-8-sig"))


def percentile(values, p):
    values = sorted(values)
    return values[max(0, min(len(values) - 1, math.ceil(len(values) * p) - 1))] if values else 0


def check_soak(root, seconds=600, cooked=False):
    reports = {}
    for role in ("Server", "A", "B", "C", "D"):
        report = read(root / f"{role}.json")
        assert report["role"] == role and report["failures"] == 0
        assert report["durationSeconds"] >= seconds - 1
        assert all(c.startswith("PASS ") for c in report["checks"])
        assert not cooked or report["cooked"], "Editor reports are not package evidence"
        if role == "Server":
            raw = report["serviceWindowsMs"]
            assert report["full256Samples"] == len(raw) >= seconds * 4
            assert report["fieldCycles"] >= seconds / 3 and report["reservationCycles"] > 0
            pairs = (("serviceP95Ms", raw, .95), ("serviceP99Ms", raw, .99), ("serviceMaxMs", raw, 1))
        else:
            raw = report["frameIntervalsMs"]
            assert len(raw) > seconds * 5
            assert report["networkInBytes"] > 0 and report["networkOutBytes"] > 0
            assert report["snapshotAgesMs"] and report["roundTripsMs"]
            pairs = (("frameP95Ms", raw, .95), ("frameP99Ms", raw, .99), ("frameMaxMs", raw, 1),
                     ("roundTripP95Ms", report["roundTripsMs"], .95), ("snapshotAgeP95Ms", report["snapshotAgesMs"], .95))
        for key, values, p in pairs:
            assert all(math.isfinite(v) and v >= 0 for v in values)
            assert math.isclose(report[key], percentile(values, p), abs_tol=1e-5), (role, key)
        reports[role] = {key: value for key, value in report.items() if not isinstance(value, list)}
    return {"reports": reports, "whole_service_proxy_goal_met": reports["Server"]["serviceP95Ms"] <= 2 and reports["Server"]["serviceP99Ms"] <= 5,
            "scope": "Whole authority/view samples plus diagnostic command costs, accumulated in approximately100ms wall windows. Transport and client frame intervals separate. Rendered-client count is recorded by the runner Environment.json. This does not isolate added environment cost or certify human/readability/full release acceptance."}


if __name__ == "__main__":
    result = check_soak(Path(sys.argv[1]), int(sys.argv[2]) if len(sys.argv) > 2 else 600, "--cooked" in sys.argv)
    print(json.dumps(result, indent=2))
