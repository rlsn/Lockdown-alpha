import json, auto_empower_sim as ae
c=json.load(open(ae.CONTENT,encoding="utf-8"))
B=list(ae.BUILDS)
out={}
for passive in (5.0,2.5,0.0):
    for C in (40,60,80):
        T=max(60,C)
        for S in (30,):
            P={"T":T,"C":C,"passive":passive}
            g={b:{**ae.short(c,b,P,lambda r,s=S: float(s),n=150),**ae.sustained(c,b,P,float(S),n=25)} for b in B}
            k=f"p{passive}_T{T}_C{C}_S{S}"; out[k]=g
            print(k,"emp/fight",[g[b]["emp_per_fight"] for b in B],"first",[g[b]["first_median"] for b in B],"interval",[g[b]["emp_interval_s"] for b in B],"share",[g[b]["host_emp_share_pct"] for b in B],"income",[g[b]["income_per_s"] for b in B],"waste",[g[b]["wasted_per_s"] for b in B])
json.dump(out,open("auto_empower_passive.json","w",encoding="utf-8"),ensure_ascii=False,indent=1)
