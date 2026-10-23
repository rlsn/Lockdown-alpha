"""Independently replay the B61 JSON evidence (all int64 values are decimal strings)."""
import json
import sys
from pathlib import Path

def verify(path):
    data=json.loads(Path(path).read_text(encoding="utf-8-sig"))
    accounts={a["id"]:int(a["endowment"]) for a in data["accounts"]}
    items={}
    budgets={b["id"]:b["initial"] for b in data["budgets"]}
    mint=burn=0
    seen=set()
    for serial,r in enumerate(data["receipts"],1):
        assert int(r["serial"])==serial and r["id"] not in seen
        seen.add(r["id"])
        assert sum(int(d["delta"]) for d in r["gold"])==int(r["mint"])-int(r["burn"])
        assert len({d["account"] for d in r["gold"]})==len(r["gold"])
        for d in r["gold"]:
            accounts[d["account"]]+=int(d["delta"])
            assert accounts[d["account"]]>=0
        mint+=int(r["mint"]);burn+=int(r["burn"])
        if r["units"]:
            budgets[r["budget"]]-=r["units"]
            assert budgets[r["budget"]]>=0
        for d in r["items"]:
            if d["op"]==0:
                assert d["id"] not in items
                items[d["id"]]={"kind":d["kind"],"location":d["to"],"consumed":False,"origin":r["id"]}
            else:
                i=items[d["id"]]
                assert not i["consumed"] and i["location"]==d["from"] and i["kind"]==d["kind"]
                i["location"]=d["to"]
                i["consumed"]=d["op"]==2
    assert accounts=={a["id"]:int(a["gold"]) for a in data["accounts"]}
    assert sum(accounts.values())==int(data["start"])+mint-burn==int(data["end"])
    assert (mint,burn)==(int(data["minted"]),int(data["burned"]))
    assert len(items)==len(data["items"])
    for i in data["items"]:
        assert items[i["id"]]=={k:i[k] for k in ("kind","location","consumed","origin")}
        for parent in i["parents"]:
            assert items[parent]["consumed"]
    assert budgets=={b["id"]:b["remaining"] for b in data["budgets"]}
    print(f"PASS {path}: {len(seen)} receipts, {len(accounts)} accounts, {len(items)} unique items, {sum(accounts.values())} gold")

if __name__=="__main__":
    for p in sys.argv[1:]:
        verify(p)
