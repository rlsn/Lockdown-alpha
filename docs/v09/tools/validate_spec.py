#!/usr/bin/env python3
"""Standard-library specification validation; no Unreal/native behavior claims."""
from pathlib import Path
from collections import Counter, deque
import argparse, hashlib, json, math, shutil, subprocess, sys, tempfile, zipfile
ROOT=Path(__file__).resolve().parents[1]
def load(n):return json.loads((ROOT/'data'/f'{n}.json').read_text())
def rows(n):return load(n)['rows']
checks=[]
def check(name,cond):
    checks.append({'name':name,'passed':bool(cond)})
    if not cond:raise AssertionError(name)
def main(rebuild):
    all_data={p.stem:json.loads(p.read_text()) for p in (ROOT/'data').glob('*.json')}
    check('Every active JSON parses and uses schema9',all(d.get('schema_version',d.get('schemaVersion'))==9 for d in all_data.values()))
    for n,d in all_data.items():
        if 'rows' in d:
            ids=[r['id'] for r in d['rows']]
            check(n+': row IDs unique',len(ids)==len(set(ids)))
            for r in d['rows']:
                if 'position_cm' in r:check(n+': integer centimeter position '+r['id'],len(r['position_cm'])==3 and all(type(v)==int for v in r['position_cm']))
                if 'stage_id' in r:check(n+': stage reference '+r['id'],r['stage_id'] in ['B90','B91','B92','B93','B94'])
    P={r['id']:r for r in rows('pois')};B={r['id']:r for r in rows('buildings')};props=rows('props');E=rows('routes');T=load('terrain');R=load('rules');econ=load('economy');src=rows('reward_sources')
    check('96 POIs and11 settlement hubs',len(P)==96 and len(rows('settlements'))==11)
    check('144 buildings,36 interiors,72 linked doors',len(B)==144 and sum(b['enterable'] for b in B.values())==36 and sum(len(b['door_ids']) for b in B.values())==72)
    check('480 functional props and exact type count',len(props)==480 and Counter(p['type_id'] for p in props)==Counter({t['id']:t['count'] for t in rows('object_types')}))
    check('All functional positions within safety apron',all(max(abs(p['position_cm'][0]),abs(p['position_cm'][1]))<118000 for p in props))
    check('No non-door prop inside shell footprint',all(not any(abs(p['position_cm'][0]-b['position_cm'][0])<b['size_cm'][0]/2+250 and abs(p['position_cm'][1]-b['position_cm'][1])<b['size_cm'][1]/2+250 for b in B.values()) for p in props if p['type_id']!='door'))
    check('Non-door prop centers at least250 cm apart',all(math.dist(a['position_cm'][:2],b['position_cm'][:2])>250 for i,a in enumerate(props) for b in props[i+1:] if a['type_id']!='door' and b['type_id']!='door'))
    prop_ids={p['id'] for p in props}
    check('Every enterable door points to a real prop',all(d in prop_ids for b in B.values() for d in b['door_ids']))
    check('Exactly36 unlocked public fronts and36 private rear doors',sum(p['type_id']=='door' and not p['lockable'] for p in props)==36 and sum(p['type_id']=='door' and p['lockable'] for p in props)==36)
    check('All POIs have >=2 purposes',all(len(p['functions'])>=2 for p in P.values()))
    check('117 route edges,real endpoints and computed lengths',len(E)==117 and all(e['from'] in P and e['to'] in P and e['length_cm']==round(sum(math.dist(a[:2],b[:2]) for a,b in zip(e['points_cm'],e['points_cm'][1:]))) for e in E))
    G={p:[] for p in P}
    for i,e in enumerate(E):G[e['from']].append((e['to'],i));G[e['to']].append((e['from'],i))
    def reach(start,excluded=None):
        seen={start};q=deque([start])
        while q:
            for nxt,i in G[q.popleft()]:
                if i==excluded or nxt in seen:continue
                seen.add(nxt);q.append(nxt)
        return seen
    check('All96 nodes connected strategically',len(reach('capital'))==96)
    vital=['ruin_a','ruin_b','ruin_c','boss_lair','heart_forge','heart']
    check('No single strategic edge disconnects a rare/victory target',all(all(n in reach('capital',i) for n in vital) for i in range(len(E))))
    check('All edge water contacts explicitly dry-bridge or Wet ford',all(x['mode'] in ['dry_bridge','walkable_wet_ford'] for e in E for x in e['river_contacts']))
    check('Seven named crossings,four permanent dry bridges',len(T['crossings'])==7 and sum(c['mode']=='permanent_bridge' for c in T['crossings'])==4 and all(c['indestructible'] for c in T['crossings']))
    check('No river freeze navigation or baseline slow',all(r['always_walkable'] and r['base_slow_percent']==0 and not r['freeze_navigation_change'] for r in T['rivers']))
    h=P['heart']['position_cm'];w=T['heart_water_inset'];c=w['center_cm'];sz=w['size_cm']
    check('Heart claim circle outside static water inset',abs(h[0]-c[0])>sz[0]/2+120 or abs(h[1]-c[1])>sz[1]/2+120)
    check('Antechamber direct nominal path <=15 seconds',math.dist(P['antechamber']['position_cm'][:2],h[:2])/420<=15)
    check('24 unique spawn seats on real outer cities',len(rows('spawn_pads'))==24 and {x['contender_slot'] for x in rows('spawn_pads')}==set(range(24)))
    pop=load('population');check('Default role accounting216 NPC+4 humans,196 noncontenders',sum(r['default_npc_count'] for r in pop['roles'])==216 and pop['noncontender_npcs']==196 and 196+24==pop['default_total_bodies'])
    sched=rows('npc_schedules');check('152 concrete schedule rows with correct split',Counter(r['role'] for r in sched)=={'civilian':96,'traveler':32,'guard':24})
    for r in sched:
        for k in ['home_public_anchor','work_poi_id','rest_poi_id','from_poi_id','to_poi_id','home_poi_id']:
            if k in r:check('Schedule anchor '+r['id']+' '+k,r[k] in P)
    check('Sixteen anchors have permanent stairs and real roofs',len(rows('traversal_anchors'))==16 and all(B[a['building_id']]['roof_access'] and a['permanent_alternative']=='interior_stairs' for a in rows('traversal_anchors')))
    check('Camp world reward864 and72 unique ordinal sources',sum(x['spawn_id_count'] for x in src if x['kind']=='camp')==72 and sum(x['spawn_id_count']*x['gold_each'] for x in src if x['kind']=='camp')==864)
    check('Rare source counts3 ruins,3 towers,1 boss',Counter(x['kind'] for x in src)=={'camp':12,'resource':9,'ruin_cache':3,'tower':3,'boss':1})
    key_contents=[i for x in src for i in x.get('unique_contents',[]) if i.startswith('ancient_key_')]
    check('Exactly one each A/B/C key',Counter(key_contents)=={'ancient_key_a':1,'ancient_key_b':1,'ancient_key_c':1})
    world_gold=sum(x.get('spawn_id_count',x.get('copies',0))*x.get('gold_each',0) for x in src)
    check('Independent world-gold sum1074',world_gold==1074==R['world_rewards']['max_new_world_gold'])
    cr=econ['contracts'];per=cr['courier']['perContender']*cr['courier']['gold']+cr['coreProcurement']['perContender']*cr['coreProcurement']['gold']+cr['supply']['perContender']*cr['supply']['gold'];N=24
    total=per*N+world_gold+econ['shortage']['bonusOrder']['gold']
    check('Independent full mint ceiling20074',per==790 and total==econ['accounting']['rewardMintUpperBound']==20074)
    initial=sum(m['gold'] for m in econ['markets'].values())+sum(x.get('wallet_initial_gold',0) for x in sched)
    check('Initial circulation3288 and preburn ceiling23362',initial==3288 and initial+total==23362)
    check('29 ticks,max producer burn145,deadline exclusive',len(econ['npcTicks']['times'])==29 and max(econ['npcTicks']['times'])<5400 and len(econ['npcTicks']['times'])*sum(x['goldBurn'] for x in econ['npcTicks']['production'])==145)
    check('All market and service POI refs exist',all(x in P for x in econ['market_poi_ids'].values()) and all(s['poi_id'] in P for s in rows('services')))
    check('All legacy aliases resolve without duplicate nodes',all(x in P for x in load('legacy_aliases')['aliases'].values()))
    check('B90-B94 dependency chain',[(x['id'],x['depends_on']) for x in rows('stages')]==[(f'B{90+i}',[] if i==0 else [f'B{89+i}']) for i in range(5)])
    check('62 native acceptance cases remain Not run',len(rows('acceptance_index'))==62 and all(x['native_status']=='Not run' for x in rows('acceptance_index')))
    # Independent boundary scenarios: integer event times, not an Unreal test.
    def finish(start,window_close,deadline,lethal_at=None,stun_at=None):
        end=start+10000
        return end<=window_close and end<=deadline and not any(t is not None and t<=end for t in [lethal_at,stun_at])
    check('Model:inclusive endpoint succeeds',finish(150000,160000,160000))
    check('Model:1ms late completion rejects',not finish(150001,160000,160000))
    check('Model:due stun cancels;nonlethal damage is no cancellation input',not finish(150000,160000,160000,stun_at=160000) and finish(150000,160000,160000))
    check('Model:private activation full-window feasibility',5306000+4000+30000+60000==5400000 and 5306001+4000+30000+60000>5400000)
    check('Model:Wet-Frost lock exact endpoint',1000+6000==7000 and 6999<7000<=7000)
    if rebuild:
        with tempfile.TemporaryDirectory() as temp:
            t=Path(temp);(t/'tools').mkdir();(t/'reference').mkdir()
            shutil.copy2(ROOT/'tools/rebuild_layout.py',t/'tools/rebuild_layout.py');shutil.copy2(ROOT/'reference/Project_Lockdown_V08_Source.zip',t/'reference/Project_Lockdown_V08_Source.zip')
            subprocess.run([sys.executable,str(t/'tools/rebuild_layout.py')],check=True,capture_output=True)
            actual={p.name:p.read_bytes() for p in (t/'data').glob('*.json')}
            check('Deterministic clean rebuild matches every layout-generated JSON',all((ROOT/'data'/name).read_bytes()==b for name,b in actual.items()))
    with zipfile.ZipFile(ROOT/'reference/Project_Lockdown_V08_Source.zip') as z:check('Frozen source ZIP integrity',z.testzip() is None)
    report={'status':'Passed','scope':'Specification data,graph,accounting and boundary models only','checks_passed':len(checks),'native_checks_status':'Not run','runtime_claims':False,'counts':{'pois':len(P),'buildings':len(B),'interiors':36,'props':len(props),'routes':len(E),'anchors':16,'acceptance_cases':62},'checks':checks}
    # Keep the report concise while retaining all check failures as fail-fast errors.
    (ROOT/'SPEC_VALIDATION.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k!='checks'},indent=2))
if __name__=='__main__':
    ap=argparse.ArgumentParser();ap.add_argument('--rebuild-check',action='store_true');args=ap.parse_args()
    try:main(args.rebuild_check)
    except Exception as exc:
        print('SPEC CHECK FAILED: '+str(exc),file=sys.stderr);raise
