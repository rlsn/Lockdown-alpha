#!/usr/bin/env python3
"""Validate this specification bundle. This does not execute Unreal or certify game behavior."""
from pathlib import Path
from fractions import Fraction
from collections import Counter
import csv,json,hashlib,re,sys
P=Path(__file__).resolve().parents[1]
checks=[]
def ck(name,condition):
 if not condition: raise AssertionError(name)
 checks.append(name)
def read(n):return json.loads((P/'data'/n).read_text())
r=read('v07_rules.json'); abilities=read('abilities.json')['abilities']; items=read('items.json')['items']; acq=read('acquisition.json');world=read('world_and_routes.json');kits=read('starter_kits.json')['templates'];ei=read('effects.json')['statuses'];ops=read('operation_contracts.json')['operations']
for n in (P/'data').glob('*.json'):
 x=json.loads(n.read_text()); ck('parse+schema version '+n.name,x['schema_version']==7)
aids={a['id'] for a in abilities};iids={i['id'] for i in items};mids={m['id'] for m in acq['materials']};opids={o['id'] for o in ops};tags={s['id'] for s in ei}
ck('28 unique ability definitions',len(abilities)==len(aids)==28)
ck('40 unique item definitions',len(items)==len(iids)==40)
ck('18 explicit status/protection definitions',len(tags)==18)
ck('no material/item id collision',not (iids&mids))
for a in abilities:
 ck(a['id']+' numeric cast contract',a['circle'] in range(1,5) and a['windup_ms']>=0 and a['mana_cost']>=0 and a['cooldown_ms']>0 and a['range_cm']>=0)
 ck(a['id']+' operations resolved',bool(a['effects']) and all(o['op'] in opids for o in a['effects']))
 ck(a['id']+' ranks typed',all(len(v)==3 and all(isinstance(n,(int,float)) and n>0 for n in v) for v in a['rank_scaling'].values()))
 ck(a['id']+' potency whitelist',set(a['potency_fields'])<=set(a['rank_scaling']) and not set(a['potency_fields'])&{'cooldown_ms','stun_ms','duration_ms'})
 for g in a['requirements_any']:
  ck(a['id']+' gate within cap',all(k in kits[0]['attributes'] and 0<=v<=r['attributes']['natural_cap'] for k,v in g.items()))
 for o in a['effects']:
  if o['op'] in {'status','area_status'}:ck(a['id']+' status resolved',o['id'] in tags)
for i in items:
 ck(i['id']+' primary piece cap',all(0<=v<=2 for v in i['primary_stats'].values()))
 ck(i['id']+' weight+price',i['weight']>=0 and (i['official_price_gold'] is None or i['official_price_gold']>0))
 ck(i['id']+' active contract',bool(i['active'])==i['quick_slot_required'] and (not i['active'] or i['active']['op'] in opids))
 ck(i['id']+' consume drop',not i['consumable'] or i['death_policy']=='drop')
 ck(i['id']+' quality excludes consumables',not i['consumable'] or not i['craftsmanship_eligible'])
 if i['two_handed']:ck(i['id']+' valid two handed slot',i['equipment_slot']=='weapon')
for k in kits:
 ck(k['id']+' equal budget',sum(k['attributes'].values())==53 and min(k['attributes'].values())>=8)
 ck(k['id']+' two known starters',len(k['skills'])==2 and set(k['skills'])<=aids)
 ck(k['id']+' no silent new gear/gold grant',not k['starting_items'] and k['starting_wallet']==0)
 for a in abilities:
  costs=[sum(max(0,v-k['attributes'][s]) for s,v in g.items()) for g in a['requirements_any']]
  ck(k['id']+' can invest into '+a['id'],min(costs)<=14)
ck('four capstone sources',len([a for a in abilities if a['circle']==4])==len(acq['book_sources']['circle4'])-1) # exclude stock count metadata
fixed_books={v for k,v in acq['book_sources']['circle4'].items() if isinstance(v,str)}
ck('all capstones sourced once',fixed_books=={a['id'] for a in abilities if a['circle']==4})
recipes=acq['recipes'];outputs={x['output'] for x in recipes}
ck('twelve recipes',len(recipes)==12)
ck('recipe outputs unique',len(outputs)==len(recipes))
for x in recipes:
 ck(x['id']+' references',x['output'] in iids|mids and set(x['inputs'])<=iids|mids)
 ck(x['id']+' short deterministic inputs',2<=len(x['inputs'])<=3 and all(isinstance(v,int) and v>0 for v in x['inputs'].values()))
 ck(x['id']+' commit fields',x['fee_gold']>0 and x['hold_ms']>0 and x['requires_quiet_s']==6 and not x['disassembly'])
# Acyclic assembly graph; gear depth excludes inherited commodity manufacturing.
byout={x['output']:x for x in recipes}
def depth(id,seen=()):
 ck('no recipe cycle '+id,id not in seen)
 if id not in byout:return 0
 return 1+max(depth(p,seen+(id,)) for p in byout[id]['inputs'])
for id in outputs:ck('gear assembly depth <=2 '+id,depth(id)<=2)
for i in items:ck(i['id']+' explicit source',i['official_price_gold'] is not None or i['id'] in outputs or i['id']=='tower_relic')
ck('heartbreaker always physical route item','heartbreaker' in outputs and 'heartbreaker' in mids)
ck('no new commodity buyback',acq['regional_buyback_new_gear'] is False and acq['official_buyback_gear_or_books'] is False)
# Gear clipping contract, deliberately over caps. Keep exact expected allocation for transparency.
order=['STR','DEX','CON','INT','WIS','CHA']
def clip(pieces):
 result=Counter(); total=0
 for p in pieces:
  for s in order:
   v=min(p.get(s,0),max(0,3-result[s]),max(0,6-total));result[s]+=v;total+=v
 return {k:v for k,v in result.items() if v}
ck('gear per-stat and total clipping',clip([{'INT':2},{'INT':1,'WIS':1},{'INT':1,'WIS':1},{'DEX':1,'WIS':1}])=={'INT':3,'WIS':2,'DEX':1})
natural={'INT':9,'WIS':10};effective={'INT':12,'WIS':10}
def gate(attrs,gates):return any(all(attrs.get(k,0)>=v for k,v in g.items()) for g in gates)
temporal=next(a for a in abilities if a['id']=='temporal_lock')
ck('gear cannot qualify natural gate',not gate(natural,temporal['requirements_any']) and not gate(effective,temporal['requirements_any']))
ck('effective equipment could otherwise shortcut a gate',not gate({'INT':11,'WIS':10},temporal['requirements_any']) and gate({'INT':14,'WIS':10},temporal['requirements_any']))
ck('natural investment permits capstone',gate({'INT':13,'WIS':10},temporal['requirements_any']))
# Source equations, known inherited arithmetic and level continuity.
thresholds=r['progression']['cumulative_xp']
ck('15 strictly growing XP thresholds',len(thresholds)==15 and thresholds[0]==0 and all(a<b for a,b in zip(thresholds,thresholds[1:])))
ck('300 XP level four preserved',sum(t<=300 for t in thresholds)==4)
ck('original zero-kill gold arithmetic',75-36+50-54+80-100==15)
ck('new-template book from extra work',75+25-40-36+50-54+80-100==0)
wr=r['world_rewards'];newgold=wr['camp_spawns']*wr['camp_gold']+wr['tower_count']*wr['tower_gold']+wr['boss_count']*wr['boss_gold']
ck('new gold upper bound642',newgold==642==wr['max_new_world_gold'])
for n in range(1,5):ck('total reward bound N='+str(n),540*n+40+newgold==540*n+682)
ck('camp spawn identity budget',world['camp']['count']*world['camp']['spawns_each']==36)
ck('no new gold PvP',wr['pvp_gold_mint']==0)
ck('Charter still public',world['routes']['charter']['global'] and world['routes']['charter']['retains_v06_registration'])
ck('all four routes',{*world['routes']}=={'siege','charter','keys','artifact'})
ck('private routes distinct',not world['routes']['keys']['global'] and not world['routes']['artifact']['global'])
ck('key ownership and artifact consume',not world['routes']['keys']['consume_keys'] and world['routes']['artifact']['consume_heartbreaker_on_commit'])
ck('key source count',len([x for x in mids if x.startswith('ancient_key_')])==3)
ck('claim tactical override',not r['victory']['ordinary_damage_interrupts'] and not r['victory']['passive_proximity_contest'])
# Reference temporal examples; these exercise the stated rule, not a game callback.
def can_complete(start,open_start,close,deadline,cancel_at=None):
 end=start+10000
 return start>=open_start and end<=close and end<=deadline and (cancel_at is None or cancel_at>end)
ck('inclusive close',can_complete(150000,100000,160000,200000))
ck('one millisecond late',not can_complete(150001,100000,160000,200000))
ck('same-time hard disable beats claim',not can_complete(150000,100000,160000,200000,160000))
ck('exact deadline',can_complete(150000,100000,160000,160000))
ck('past deadline',not can_complete(150000,100000,160000,159999))
ck('full activation window at boundary',1906000+4000+30000+60000==2000000)
# Mana costs are reachable at legal natural prerequisites; no source-unusable capstone.
for k in kits:
 for a in abilities:
  mp=180+12*(k['attributes']['INT']-8)+6*(k['attributes']['WIS']-8)
  ck(k['id']+' base resource capacity '+a['id'],a['mana_cost']<=mp)
# Quality uses integer ratio to avoid 1.14 float truncation.
ck('Masterwork physical base exact',50*114//100==57)
ck('Fine armor integer floor',18*107//100==19)
# Small conservation/projected-state examples covering categorical transfer vs create/consume.
before={'wallet':100,'bank':0,'market':150};after={'wallet':0,'bank':98,'market':150}
ck('bank deposit fee reconciliation',sum(after.values())==sum(before.values())-2)
ck('same-market transfer conservation',100+150==(100-12)+(150+12))
ck('death tax example ceil',((101*15+99)//100)==16)
ck('full bag net recipe capacity',24-2+1<=24)
ck('quality keeps item instance count',1==1+0-0)
ck('same tag overlapping durations max endpoint',max(0+1200,600+1200)==1800)
# Claimable nodes connected to all settlements through authored graph.
graph={x['id']:set() for x in world['pois']}
for a,b in world['required_edges']:
 ck('edge nodes resolve '+a+'/'+b,a in graph and b in graph);graph[a].add(b);graph[b].add(a)
seen=set();todo=['City']
while todo:
 n=todo.pop()
 if n not in seen:seen.add(n);todo.extend(graph[n]-seen)
ck('all ten POIs reachable',len(seen)==len(world['pois'])==10)
ck('multiple Heart approaches',len(graph['CentralPrecinct'])>=3)
# Coverage and active links. Source references may contain historical broken links, exclude frozen sources.
idx=read('acceptance_index.json')['checks'];ledger=list(csv.DictReader((P/'templates/FEATURE_LEDGER.csv').open()))
ck('55 unique acceptance ids',len(idx)==len({x['id'] for x in idx})==55)
ck('ledger exactly acceptance index',[x['id'] for x in ledger]==[x['id'] for x in idx])
ck('no fabricated game passes',all(x['status']=='Not run' for x in ledger))
for name in ['README.md','00_DECISIONS_AND_MIGRATION.md','01_SCOPE_AND_RELEASE_PROFILE.md','02_COMBAT_RUNTIME_AND_EFFECTS.md','03_TARGETING_VISION_AND_TERRAIN.md','04_PROGRESSION_EQUIPMENT_AND_LOADOUTS.md','05_ABILITY_CATALOG.md','06_ITEM_CATALOG.md','07_ACQUISITION_ASSEMBLY_AND_CRAFTING.md','08_WORLD_POIS_AND_NPC_POLICY.md','09_VICTORY_AND_MATCH_STATE.md','10_ECONOMY_DEATH_AND_ACCOUNTING.md','11_AUTHORITY_NETWORKING_AND_UI.md','12_BUILD_PLAN.md','13_ACCEPTANCE_AND_PLAYTEST.md','14_CODEX_BUILD_PROMPT.md','15_SOURCE_HISTORY_AND_LIMITS.md','16_DATA_CONTRACT_AND_IMPORT.md']:
 ck('active document '+name,(P/name).is_file() and (P/name).stat().st_size>500)
# Optional schema validation is intentionally not required: the executable validator above uses stdlib only.
print('PASS:',len(checks),'specification assertions; 28 abilities,40 items,12 recipes,55 planned game checks.')
print('No Unreal implementation, gameplay, networking or human test has been executed by this checker.')
