"""Replay all four native v08 journeys and check their route-specific source receipts."""
import json
import hashlib
import sys
from collections import Counter
from pathlib import Path

from CheckEconomyEvidence import verify


def read(path):
    raw = path.read_bytes()
    return json.loads(raw.decode("utf-16" if raw[:2] in (b"\xff\xfe", b"\xfe\xff") else "utf-8-sig"))


def check(root):
    summaries = {}
    payload = Path(__file__).resolve().parents[1] / "Content/Lockdown/Data/V08/combat_b84.json"
    digest = hashlib.sha256(payload.read_bytes().replace(b"\r\n", b"\n")).hexdigest()
    for route in ("Keys", "Siege", "Artifact", "Charter"):
        report = read(root / route / "Report.json")
        ledger_path = root / route / "Ledger.json"
        ledger = read(ledger_path)
        verify(ledger_path)
        assert report["route"] == route and report["schema"] == 8
        assert report["catalogDigest"] == digest
        assert report["failures"] == 0 and all(c.startswith("PASS ") for c in report["checks"])
        assert 0 < report["seconds"] <= 2101 and len(report["travel"]) >= 5
        assert any("Single terminal winner" in c for c in report["checks"])
        causes = Counter(r["cause"] for r in ledger["receipts"])
        assert not any("diagnostic" in cause.lower() or "test" in cause.lower() for cause in causes)
        # v08 starter inventories and wallets are empty; institutions retain their
        # declared stock endowment. Every new physical identity is replayed above.
        assert not causes["CarriedEndowment"]
        if route == "Keys":
            assert causes["KeysActivation"] == 1 and causes["RuinCachePickup"] == 3
            assert int(ledger["minted"]) == int(ledger["burned"]) == 0
            assert any("Level-one paid Counterpulse interrupts" in c for c in report["checks"])
            keys = [i for i in ledger["items"] if i["kind"] in (50, 51, 52)]
            assert {i["kind"] for i in keys} == {50, 51, 52} and len(keys) == 3
            assert len({i["location"] for i in keys}) == 1
            assert all(not i["consumed"] and i["location"].startswith("Wallet:") for i in keys)
        elif route == "Siege":
            assert causes["GrandTowerDefeat"] == 3 and causes["CampDefeat"] == 1
            assert sum(i["kind"] == 47 for i in ledger["items"]) == 3
        elif route == "Artifact":
            assert causes["GuardianDefeat"] == 1
            assert any("Four physical ingredients" in c for c in report["checks"])
            artifacts = [i for i in ledger["items"] if i["kind"] == 53]
            assert len(artifacts) == 1 and artifacts[0]["consumed"]
            items = {i["id"]: i for i in ledger["items"]}
            assert sorted(items[parent]["kind"] for parent in artifacts[0]["parents"]) == [7, 7, 48, 49]
        else:
            assert any("100g registered Charter" in c for c in report["checks"])
            assert any("rival has no Charter registration" in c for c in report["checks"])
        summaries[route] = {
            "checks": len(report["checks"]), "seconds": report["seconds"],
            "travel_legs": len(report["travel"]), "receipts": len(ledger["receipts"]),
            "physical_identities": len(ledger["items"]), "minted": ledger["minted"],
            "burned": ledger["burned"], "final_gold": ledger["end"], "causes": dict(causes),
        }
    print(json.dumps(summaries, indent=2))
    return summaries


if __name__ == "__main__":
    check(Path(sys.argv[1]))
