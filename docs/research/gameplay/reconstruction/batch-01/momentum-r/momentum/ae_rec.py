import json, auto_empower_sim as ae
c=json.load(open(ae.CONTENT,encoding="utf-8"))
B=list(ae.BUILDS)
out={}
def run(tag,P,S,tau=4.0):
    ae.TAU_SKILL=tau
    g={b:{**ae.short(c,b,P,lambda r,s=S: float(s),n=200),**ae.sustained(c,b,P,float(S),n=30)} for b in B}
    out[tag]=g
    print(tag,"emp/fight",[g[b]["emp_per_fight"] for b in B],"pct",[g[b]["pct_fights_with_emp"] for b in B],"first",[g[b]["first_median"] for b in B],"interval",[g[b]["emp_interval_s"] for b in B],"share",[g[b]["host_emp_share_pct"] for b in B],"waste",[g[b]["wasted_per_s"] for b in B])
REC={"T":60,"C":50,"passive":2.5}
run("REC_T60_C50_p2.5_S30",REC,30)
run("REC_saturated",REC,30,tau=0.0)
run("REC_S50_entry",REC,50)
run("REC_hurt0.3",{**REC,"hurt_gain":0.3},30)
run("T60_C50_p3.5",{**REC,"passive":3.5},30)
run("T70_C50_p2.5",{**REC,"T":70},30)
json.dump(out,open("auto_empower_rec.json","w",encoding="utf-8"),ensure_ascii=False,indent=1)
