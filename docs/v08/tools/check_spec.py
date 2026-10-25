#!/usr/bin/env python3
"""Validate active specification/data integrity and small deterministic boundary models.
Does not validate Unreal code, engine import, networking, AI, human balance or performance.
Stdlib only. Run from any directory.
"""
from pathlib import Path
import json, hashlib, sys, unittest
from reference_models import Victim,frost,loadout_valid,applied_primary,mitigate,barrier_hit,claim_survives,field_transition
ROOT=Path(__file__).resolve().parents[1]
def read(name):return json.loads((ROOT/'data'/f'{name}.json').read_text())
A=read('abilities')['abilities'];I=read('items')['items'];R=read('v08_rules');ENV=read('environment');AC=read('acquisition');T=read('class_traits');OPS={x['id'] for x in read('operation_contracts')['operations']}
FAIL=[];N=0
def check(ok,what):
 global N
 N+=1
 if not ok:FAIL.append(what)
def unique(rows,label):
 ids=[r['id'] for r in rows];check(len(ids)==len(set(ids)),label+' duplicate IDs');return set(ids)
aids=unique(A,'ability');iids=unique(I,'item');mids=unique(AC['materials'],'material');tids=unique(T['traits'],'trait');opids=unique(read('operation_contracts')['operations'],'operation');modids=unique(read('modifier_contracts')['modifiers'],'modifier')
check((len(A),len(I),len(AC['recipes']),len(T['traits']),len(ENV['reactions']))==(52,80,20,9,10),'declared catalog counts')
for p in (ROOT/'data').glob('*.json'):check(json.loads(p.read_text()).get('schema_version')==8,str(p.name)+' schema')
check(R['profiles']['v08_integration']['skill_slots']==6 and R['profiles']['v08_integration']['accessory_slots']==4,'six plus four')
check(len(R['inventory']['equipment_slots'])==11,'eleven physical bindings')
check(ENV['rivers_always_walkable'] and not ENV['dynamic_navigation_updates_from_elements'],'river graph invariant')
check(ENV['max_reaction_depth']==1 and ENV['cell_states']==['wet','frozen','burning','electrified','obscured','overgrown'],'bounded state set')
statuses=unique(read('effects')['statuses'],'status')
allowed_targets={'hostile_unit','self','ground','self_or_observed_unit','line','anchor_or_hostile_unit','cone','owned_marker','two_observed_units','two_ground_points','self_area','self_or_ground','observed_construct'}
for a in A:
 check(a['activation_kind']=='learned_active','active-only '+a['id'])
 check(a['targeting'] in allowed_targets,'targeting '+a['id'])
 check(isinstance(a['requirements_any'],list) and bool(a['requirements_any']),'gate '+a['id'])
 for group in a['requirements_any']:
  check(all(k in ['STR','DEX','CON','INT','WIS','CHA'] and 0<=v<=16 for k,v in group.items()),'gate attrs '+a['id'])
 for k in ['mana_cost','windup_ms','cooldown_ms','range_cm','area_cm']:check(isinstance(a[k],int) and a[k]>=0,'numeric '+a['id']+':'+k)
 check(all(len(v)==3 and all(isinstance(x,(int,float)) and x>=0 for x in v) for v in a['rank_scaling'].values()),'rank bases '+a['id'])
 check(all(k in a['rank_scaling'] for k in a['potency_fields']),'potency field '+a['id'])
 check(a['acquisition']['circle']==a['circle'],'book source '+a['id'])
 for e in a['effects']:
  check(e['op'] in OPS,'unknown op '+a['id']+':'+e['op'])
  if e['op'] in ['status','area_status']:check(e['id'] in statuses,'unknown status '+a['id'])
  if e.get('direct_frost'):check(e['op']=='element_hit' and e.get('element')=='frost','direct frost '+a['id'])
  if e['op']=='surface_apply':check(0<e['max_cells']<=ENV['max_cells_per_cast'],'cell footprint '+a['id'])
for i in I:
 check(i['equipment_slot'] in ['head','torso','legs','feet','gloves','main_hand','off_hand','accessory'],'physical slot '+i['id'])
 check(not i['quick_slot_required'],'no independent quick binding '+i['id'])
 check(i['weight']>=0 and isinstance(i['weight'],int),'weight '+i['id'])
 check(all(k in ['STR','DEX','CON','INT','WIS','CHA'] and 0<v<=2 for k,v in i['primary_stats'].items()),'stats '+i['id'])
 check(not i['two_handed'] or i['equipment_slot']=='main_hand','handedness '+i['id'])
 check(not i['consumable'] or i['equipment_slot']=='accessory' and i['death_policy']=='drop','consumable '+i['id'])
 if i['active']:
  check(i['active']['op'] in OPS,'item op '+i['id'])
  check(i['activation_binding']==('accessory' if i['equipment_slot']=='accessory' else 'shared_skill'),'action binding '+i['id'])
  check('targeting' in i['active'] and 'windup_ms' in i['active'],'item target/windup '+i['id'])
 else:check(i['activation_binding'] is None,'passive action '+i['id'])
 if 'modifier' in i['passive']:check(i['passive']['modifier'] in modids,'modifier ref '+i['id'])
 if i['official_price_gold'] is not None:check(i['official_price_gold']>0,'not free '+i['id'])
 else:check(i['acquisition']['kind'] in ['recipe','tower_drop'],'scarce source '+i['id'])
 check(not i['craftsmanship_eligible'] or bool(i['quality_fields']),'quality eligibility '+i['id'])
for k in read('starter_kits')['templates']:
 check(sum(k['attributes'].values())==53,'budget '+k['id']);check(k['trait_id'] in tids,'traitref '+k['id']);check(set(k['skills'])<=aids,'starter skills '+k['id'])
check(all(not t['breakable'] and not t['signature_exclusive'] for t in T['traits']),'trait restrictions')
recipes=AC['recipes'];rids=unique(recipes,'recipe');definitions=iids|mids
for r in recipes:
 check(r['output'] in definitions,'recipe output '+r['id']);check(set(r['inputs'])<=definitions,'recipe input '+r['id']);check(all(v>0 for v in r['inputs'].values()),'positive inputs '+r['id']);check(r['fee_gold']>0,'fee '+r['id'])
for i in I:
 if i['acquisition']['kind']=='recipe':check(i['acquisition']['recipe_id'] in rids,'recipe source '+i['id'])
# Syntactic reachability under actual source policy; does not prove finite materials suffice for every player.
known={i['id'] for i in I if i['official_price_gold'] is not None or i['acquisition']['kind']=='tower_drop'}|{m['id'] for m in AC['materials'] if m['id']!='heartbreaker'}
while True:
 new={r['output'] for r in recipes if set(r['inputs'])<=known}
 if new<=known:break
 known|=new
check(definitions<=known,'unreachable definitions '+str(sorted(definitions-known)))
# Any recipe-only dependency cycle is a contract failure even if a priced alternative happens to break it.
graph={r['output']:set(r['inputs']) for r in recipes}
def visit(node,path):
 check(node not in path,'cycle '+str(path+[node]))
 if node in path:return
 for dep in graph.get(node,()):
  if dep in graph:visit(dep,path+[node])
for node in graph:visit(node,[])
for a in A:check(a['id'] in (ROOT/'06_ABILITY_CATALOG.md').read_text(),'ability table coverage '+a['id'])
for i in I:check(i['id'] in (ROOT/'07_ITEM_CATALOG.md').read_text(),'item table coverage '+i['id'])
accept=read('acceptance_index');check(len(accept['new_cases'])==68 and len(accept['inherited_required_cases'])==55,'acceptance counts')
check(all(c['status'].startswith('Not run') for c in accept['new_cases']),'honest native status')
for p in json.loads((ROOT/'SOURCE_MANIFEST.json').read_text())['inputs']:check(hashlib.sha256((ROOT/p['file']).read_bytes()).hexdigest()==p['sha256'],'source archive hash')
check(R['world_rewards']['max_new_world_gold']==642 and R['victory']['claim_ms']==10000,'unchanged economy/outcome budgets')

class BoundaryExamples(unittest.TestCase):
 def test_wet_direct(self):
  v=Victim(wet=True);self.assertEqual(frost(v,1000,600,6000),'stunned');self.assertFalse(v.wet);self.assertEqual((v.stun_until,v.ready_at),(1600,7000))
 def test_surface_not_direct(self):
  v=Victim(wet=True)
  for t in range(0,10000,100):self.assertEqual(frost(v,t,600,6000,direct=False),'no_reaction')
  self.assertEqual(v.ready_at,0)
 def test_shared_boundary(self):
  v=Victim(wet=True);frost(v,1000,600,6000);v.wet=True
  self.assertEqual(frost(v,6999,600,6000),'no_reaction');self.assertEqual(v.ready_at,7000);self.assertEqual(frost(v,7000,600,6000),'stunned')
 def test_guard_attempt(self):
  v=Victim(wet=True,guard=True);self.assertEqual(frost(v,1000,600,6000),'guarded');self.assertEqual(v.ready_at,7000);self.assertFalse(v.guard);self.assertEqual(v.stun_until,0)
 def test_spell_block(self):
  v=Victim(wet=True,guard=True);self.assertEqual(frost(v,1000,600,6000,blocked=True),'no_reaction');self.assertTrue(v.wet and v.guard);self.assertEqual(v.ready_at,0)
 def test_dry_or_self(self):
  self.assertEqual(frost(Victim(),0,600,6000),'no_reaction');self.assertEqual(frost(Victim(wet=True),0,600,6000,hostile=False),'no_reaction')
 def test_immune_attempt(self):
  v=Victim(wet=True);self.assertEqual(frost(v,0,600,6000,immune=True),'immune');self.assertEqual(v.ready_at,6000)
 def test_loadout_cap(self):
  s=[('learned',str(i)) for i in range(6)]
  self.assertTrue(loadout_valid(s,list('abcd'),set()));self.assertFalse(loadout_valid(s+[('learned','7')],list('abcd'),set()));self.assertFalse(loadout_valid(s,list('abcde'),set()))
 def test_source_binding(self):
  self.assertFalse(loadout_valid([('gear','mail')],[],set()));self.assertTrue(loadout_valid([('gear','mail')],[],{'mail'}));self.assertFalse(loadout_valid([('learned','x'),('learned','x')],[],set()))
 def test_primary_budget(self):
  attrs=['STR','DEX','CON','INT','WIS','CHA'];order=R['inventory']['binding_order']
  result=applied_primary({'head':{'STR':4,'DEX':2},'torso':{'STR':2,'CON':2},'main_hand':{'INT':2}},order,attrs)
  self.assertEqual(result,{'STR':3,'DEX':2,'CON':1,'INT':0,'WIS':0,'CHA':0})
 def test_claim_distinctions(self):
  self.assertTrue(claim_survives(damage=80,wet=True,slow=True,root=True,silence=True,mute=True));self.assertFalse(claim_survives(stun=True));self.assertFalse(claim_survives(forced_move=1));self.assertTrue(claim_survives(forced_move=0));self.assertFalse(claim_survives(explicit_interrupt=True))
 def test_melt_no_fire_recursion(self):
  out=field_transition({'frozen','wet'},'fire','water');self.assertEqual(out,{'wet','obscured'});self.assertNotIn('burning',out)
 def test_conduction_frozen(self):
  self.assertNotIn('electrified',field_transition({'frozen','wet'},'lightning','water'));self.assertIn('electrified',field_transition({'wet'},'lightning'))
 def test_oil_fuel(self):
  self.assertEqual(field_transition({'oil'},'fire'),{'burning'});self.assertEqual(field_transition({'burning'},'water'),{'wet'})
 def test_wind_shatter(self):
  self.assertEqual(field_transition({'obscured','wet'},'wind'),{'wet'});self.assertEqual(field_transition({'frozen'},'shatter'),{'wet'})
 def test_no_terrain_state_growth(self):
  allowed=set(ENV['cell_states'])|{'oil'}
  for impulse in ['fire','frost','water','wind','lightning','nature','shatter']:
   for s in [set(),{'wet'},{'frozen','wet'},{'oil'},{'burning'},{'overgrown'}]:self.assertLessEqual(field_transition(s,impulse,'soil'),allowed)
 def test_mitigation_cap(self):
  self.assertAlmostEqual(mitigate(100,.15,.25),63.75);self.assertAlmostEqual(mitigate(100,.6,.25),40)
 def test_barrier_bonus_no_hp_spill(self):
  hp,pool=barrier_hit(100,150,.5);self.assertEqual((hp,pool),(0,0));hp,pool=barrier_hit(100,75,.5);self.assertEqual((hp,pool),(50,0))

if __name__=='__main__':
 suite=unittest.defaultTestLoader.loadTestsFromTestCase(BoundaryExamples)
 result=unittest.TextTestRunner(verbosity=1).run(suite)
 print(f'{N} specification assertions; {len(FAIL)} failures; {result.testsRun} reference boundary tests')
 for error in FAIL:print('FAIL:',error)
 if FAIL or not result.wasSuccessful():sys.exit(1)
 print('PASS: specification models only. Native/engine/network/human tests NOT RUN.')
