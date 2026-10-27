#!/usr/bin/env python3
"""Deterministically rebuild v0.9 spatial manifests. Python standard library only."""
from pathlib import Path
import copy, hashlib, io, json, math, random, zipfile

ROOT = Path(__file__).resolve().parents[1]
D = ROOT / 'data'
D.mkdir(exist_ok=True)
def write(name, value):
    (D / (name+'.json')).write_text(json.dumps(value, indent=2)+'\n')
def doc(rows): return {'schema_version':9, 'rows':rows}
def dist(a,b): return math.dist(a[:2],b[:2])
def source_data():
    with zipfile.ZipFile(ROOT/'reference/Project_Lockdown_V08_Source.zip') as z:
        base='project-lockdown-v08/'
        v8={Path(n).stem:json.loads(z.read(n)) for n in z.namelist() if n.startswith(base+'data/') and n.endswith('.json')}
        with zipfile.ZipFile(io.BytesIO(z.read(base+'reference/Project_Lockdown_V07_Source.zip'))) as v7:
            econ=json.loads(v7.read('project-lockdown-v07/reference-v06/data/economy_rules.json'))
    return v8,econ
v8,econ=source_data()
rules=copy.deepcopy(v8['v08_rules'])
rules.update(schema_version=9, date='2026-10-27',status='Design defaults; specification checks only; no engine evidence')
rules['profiles']={
 'v09_full':{'seconds':5400,'supported_test_durations_s':[3600,4500,5400],'contenders_min':24,'contenders_max':24,'humans_min':1,'humans_max':12,'default_humans':4,'default_ai_contenders':20,'level_cap':15,'skill_slots':6,'body_equipment_slots':7,'accessory_slots':4,'normal_grants':False},
 'v08_integration':copy.deepcopy(v8['v08_rules']['profiles']['v08_integration'])}
rules['world']={'bounds_cm':[-120000,-120000,120000,120000],'playable_edge_inset_cm':2000,'seed':9001,'revision':1,'coordinate_axes':{'x':'east','y':'north','z':'up'},'environment_grid_origin_cm':[-120000,-120000,0], 'body_sight_cm':1800,'public_watch_sight_cm':2400,'sight_bonus_policy':'retain v08 +50 cm cap','elevation_bands_cm':[0,400,800]}
rules['world_rewards'].update(camp_spawns=72,max_new_world_gold=1074,max_all_reward_mints='790*N+1114')
rules['environment_overrides']={'max_dynamic_cells_match':1024,'max_surface_transitions_per_batch':2048,'max_dynamic_cells_per_owner':64,'evaluation_ms':100,'reason':'full profile only; runtime stress gate required'}
rules['timing_policy']={'default_deadline_s':5400,'no_minimum_victory_time':True,'deadline_result':'NoAcquisition','warning_ms':30000,'open_ms':60000,'claim_ms':10000}
rules['discovery']={'first_settlement_visit_xp':25,'settlements_max':11,'ruin_discovery_xp':50,'ruins_max':3,'identity_scope':'match+contender+poi; no life resets'}
write('rules',rules)

settlements=[]
def settle(id,name,x,y,z,kind,identity):
    settlements.append({'id':id,'name':name,'position_cm':[x*100,y*100,z*100],'kind':kind,'footprint_radius_cm':20000 if kind=='capital' else 12500 if kind=='city' else 6000,'identity':identity,'security':'watched_core_unwatched_edges'})
settle('capital','Crownreach',40,40,4,'capital','Heart, Charter, finance; guarded squares and contested precinct')
settle('highfort','Highfort',-820,720,8,'city','fortification, training, cliff paths')
settle('scholars','Lumen Archive',780,780,4,'city','books, research materials, gardens')
settle('riverport','Riverport',-800,-660,0,'city','commerce, warehouses, smuggling edges')
settle('ironworks','Ironworks',760,-680,4,'city','ore, production, advanced forge')
for args in [('oakmere','Oakmere',-770,80,0),('millford','Millford',-390,450,0),('reedbank','Reedbank',-470,-650,0),('ashcross','Ashcross',380,-850,4),('bellwick','Bellwick',730,200,4),('stonewake','Stonewake',0,840,8)]:
    settle(*args,'town','rest, local work, route choice')
write('settlements',doc(settlements)); S={r['id']:r for r in settlements}
pois=[]
def poi(id,name,kind,pos,functions,security='wild',reward=None,legacy=None):
    r={'id':id,'name':name,'kind':kind,'position_cm':list(pos),'interaction_radius_cm':120,'functions':functions,'security':security,'reward_source':reward,'legacy_alias':legacy,'access':'ordinary_navigation_no_spell_required','public_map':'service_or_landmark_only','stage_id':'B93' if kind in ['tower','heart','terminal'] else 'B90' if kind in ['settlement','crossing','pass'] else 'B92' if kind in ['camp','ruin','boss','service','resource','shrine'] else 'B91'}
    pois.append(r);return r
for s in settlements:
    poi(s['id'],s['name'],'settlement',s['position_cm'],['route_hub','civilian_work_and_trade'],'watched')
buildings=[]
service_rows=[]
city_services={
 'capital':[('treasury','Treasury',['bank','charter_purchase','core_procurement']),('supply','Official Supply',['official_supply','circle1_books']),('guild','Civic Guild',['supply_commission','courier_board','witness']),('workshop','Commons Workshop',['workshop','preparation']),('infirmary','Public Infirmary',['shrine','safe_route_information']),('guard','Civic Guard Post',['guards','crime_reporting'])],
 'highfort':[('barracks','Barracks',['trainer_circle2','preparation']),('supply','Garrison Supply',['official_supply','circle1_books']),('market','Highfort Market',['market','courier_board']),('workshop','Armorer Hall',['workshop','preparation']),('inn','Cliff Inn',['shrine','civilian_rest']),('guard','North Watch',['guards','crime_reporting'])],
 'scholars':[('archive','Grand Archive',['archive_circle3','circle2_books']),('supply','Scholars Supply',['official_supply','circle1_books']),('market','Garden Exchange',['market','courier_board']),('workshop','Research Hall',['workshop','preparation']),('infirmary','Garden Hospice',['shrine','civilian_rest']),('guard','Library Watch',['guards','crime_reporting'])],
 'riverport':[('market','Harbor Exchange',['market','courier_board']),('supply','Dock Supply',['official_supply','circle1_books']),('guild','Port Guild',['supply_commission','courier_board','witness']),('workshop','Shipwright Hall',['workshop','preparation']),('inn','Low Tide Inn',['shrine','civilian_rest']),('guard','Harbor Watch',['guards','crime_reporting'])],
 'ironworks':[('forge','Advanced Forge',['advanced_forge','preparation']),('supply','Foundry Supply',['official_supply','circle1_books']),('market','Foundry Exchange',['market','courier_board']),('workshop','Open Workshop',['workshop','preparation']),('inn','Cinder Inn',['shrine','civilian_rest']),('guard','Foundry Watch',['guards','crime_reporting'])]}
selected=[0,2,4,18,20,22]
for si,s in enumerate(settlements):
    city=s['kind']!='town'; scale=1.4 if s['kind']=='capital' else 1
    offsets=[(x*scale,y*scale) for y in [-60,-30,30,60] for x in [-78,-52,-26,26,52,78]] if city else [(-25,-25),(25,-25),(-25,25),(25,25)]
    for j,(dx,dy) in enumerate(offsets):
        pos=[round(s['position_cm'][0]+dx*100),round(s['position_cm'][1]+dy*100),s['position_cm'][2]]
        enterable=j in selected if city else j==0
        b={'id':f"building_{s['id']}_{j:02d}",'settlement_id':s['id'],'position_cm':pos,'size_cm':[1400,1800,600],'enterable':enterable,'floor_count':2 if enterable and j in [0,18] else 1,'roof_access':enterable and j in [0,18],'wall_vision_proxy':True,'roof_fade_cosmetic_only':True,'permanent_structural_collision':True,'door_ids':[],'stage_id':'B91'}
        buildings.append(b)
        if enterable:
            if city:
                k=selected.index(j);suffix,name,functions=city_services[s['id']][k]
                pid=s['id']+'_'+suffix
                p=poi(pid,name,'service',pos,functions,'watched' if suffix!='inn' else 'street')
                b['poi_id']=pid
                service_rows.append({'id':'service_'+pid,'poi_id':pid,'functions':functions,'protected_official':True,'queue':'unbounded_nonexclusive_service_transactions','shared_stock_pool':'global_per_definition_for_official_books_and_rare_stock','stage_id':'B92'})
            else:
                b['poi_id']=s['id']
                service_rows.append({'id':'service_'+s['id'],'poi_id':s['id'],'functions':['courier_board','workshop','preparation','shrine'],'protected_official':True,'queue':'unbounded_nonexclusive_service_transactions','shared_stock_pool':None,'stage_id':'B92'})

def at(x,y,z=0):return [round(x*100),round(y*100),round(z*100)]
camp_coords=[(-1030,400,0),(-580,920,8),(380,1020,8),(1040,510,4),(-1040,-180,0),(-600,-320,0),(460,470,4),(1010,-120,0),(-1020,-990,0),(-100,-1000,0),(480,-410,4),(1040,-980,4)]
for i,p in enumerate(camp_coords):poi(f'camp_{i+1:02d}',f'Wilderness Camp {i+1}','camp',at(*p),['bounded_combat','patrol_cover'],'wild',f'camp_{i+1:02d}')
for id,name,p,legacy in [('ruin_a','Glass Monastery',(-960,1020,8),'RuinA'),('ruin_b','Drowned Observatory',(-70,-1020,0),'RuinB'),('ruin_c','Ash Vault',(1040,-420,4),'RuinC')]:
    poi(id,name,'ruin',at(*p),['unique_key_catalyst_book_cache','ambush_and_retreat'],'wild',id,legacy)
poi('boss_lair','Hollow Crown Quarry','boss',at(90,570,4),['single_guardian_boss','fragment_and_book'],'wild','boss_once','BossLair')
poi('heart_forge','Old Heart Foundry','service',at(390,-310,4),['heart_forge','craft_interception'],'wild',None,'HeartForge')
for id,p in [('tower_a',(-90,190,4)),('tower_b',(195,10,4)),('tower_c',(10,-155,4))]:
    poi(id,'Grand '+id.replace('_',' ').title(),'tower',at(*p),['siege_gate','local_public_watch'],'contested',id,{'tower_a':'GrandTowerA','tower_b':'GrandTowerB','tower_c':'GrandTowerC'}[id])
poi('heart','Grand Heart','heart',at(40,40,4),['single_physical_claim','public_attempt_sensor'],'contested',None,'CentralPrecinct')
poi('antechamber','Heart Antechamber','terminal',at(40,-5,4),['keys_activation','heartbreaker_activation'],'contested')
strategic=[('north_bridge','North Bridge',-280,650,0,'crossing'),('mill_bridge','Mill Bridge',-360,350,0,'crossing'),('capital_ford','Crown Ford',-325,100,0,'crossing'),('south_bridge','South Bridge',-300,-560,0,'crossing'),('reed_ford','Reed Ford',-300,-820,0,'crossing'),('east_bridge','Garden Bridge',610,295,4,'crossing'),('east_ford','Bell Ford',840,223,0,'crossing'),('north_pass','North Saddle',-450,1020,8,'pass'),('west_pass','Oak Cut',-920,360,4,'pass'),('east_pass','Foundry Steps',960,-250,4,'pass'),('south_tunnel','Ash Culvert',320,-600,0,'pass'),('heart_dry_flank','Heart Dry Flank',100,-95,4,'pass')]
for id,name,x,y,z,kind in strategic:poi(id,name,kind,at(x,y,z),['route_choice','vision_or_exposure_control'],'street' if kind=='crossing' else 'wild')
resources=[('crystal_north','Sky Crystal',-250,1030,8,'Crystal'),('crystal_west','Moss Crystal',-960,100,0,'Crystal'),('crystal_center','Quarry Crystal',220,570,4,'Crystal'),('crystal_east','Garden Crystal',1000,900,4,'Crystal'),('crystal_south','Reed Crystal',-540,-1000,0,'Crystal'),('ore_north','North Ore',-1080,800,8,'Ore'),('ore_east','Foundry Ore',950,-850,4,'Ore'),('fiber_west','Oak Fiber',-980,-360,0,'Fiber'),('fiber_south','Farm Fiber',260,-1040,0,'Fiber')]
for id,name,x,y,z,good in resources:poi(id,name,'resource',at(x,y,z),['finite_'+good+'_harvest','trade_route_interception'],'wild',id)
for args in [('oak_shrine','Oak Shrine',-650,260,0),('reed_shrine','Reed Shrine',-660,-880,0),('ash_shrine','Ash Shrine',550,-1020,4),('sky_shrine','Sky Shrine',420,850,8),('quarry_shrine','Quarry Shrine',210,450,4),('west_watch','Broken West Watch',-1050,540,4),('east_watch','Abandoned East Watch',1050,80,4),('smuggler_cove','Port Quay and Smuggler Cove',-800,-805,0),('old_lab','Old Laboratory',1010,1040,4),('fallen_garden','Fallen Garden',400,680,4),('charcoal_camp','Charcoal Yard',590,-180,0),('ferry_ruins','Ferry Ruins',-450,-160,0)]:
    id,name,x,y,z=args;kind='shrine' if 'shrine' in id else 'landmark'
    poi(id,name,kind,at(x,y,z),['recovery','covered_detour'] if kind=='shrine' else ['supply_storage','tactical_vantage'],'wild')
assert len(pois)==96,len(pois)
P={p['id']:p for p in pois}
write('pois',doc(pois));write('buildings',doc(buildings));write('services',doc(service_rows))

rivers=[{'id':'main_river','width_cm':2200,'bank_ramp_width_cm':600,'floor_z_cm':0,'points_cm':[at(x,y) for x,y in [(-220,1200),(-380,950),(-270,650),(-360,360),(-320,120),(-400,-180),(-230,-420),(-350,-700),(-220,-1000),(-300,-1200)]]},
 {'id':'port_canal','width_cm':1800,'bank_ramp_width_cm':500,'floor_z_cm':0,'points_cm':[at(x,y) for x,y in [(-900,-820),(-800,-800),(-650,-770),(-520,-730),(-338,-670)]]},
 {'id':'east_tributary','width_cm':1400,'bank_ramp_width_cm':500,'floor_z_cm':0,'points_cm':[at(x,y) for x,y in [(1200,280),(800,220),(600,300),(350,240),(120,300),(-160,240),(-320,120)]]}]
for r in rivers:r.update(always_walkable=True,water_contact='v08_wet',base_slow_percent=0,freeze_navigation_change=False)
crossings=[]
for id in [x[0] for x in strategic if x[-1]=='crossing']:
    crossings.append({'id':id,'poi_id':id,'mode':'permanent_bridge' if 'bridge' in id else 'walkable_ford','width_cm':800,'indestructible':True,'applies_wet':False if 'bridge' in id else True,'river_id':'east_tributary' if id.startswith('east') else 'main_river','stage_id':'B90'})
biomes=[('north_ridge',[-1180,500,250,1180],8,'rock_pine'),('archive_uplands',[250,400,1180,1180],4,'garden_woodland'),('oakwood',[-1180,-450,-480,500],0,'dense_forest'),('reed_marsh',[-1180,-1180,0,-450],0,'marsh_farmland'),('ash_fields',[0,-1180,1180,-400],4,'quarry_scrub'),('central_basin',[-480,-400,1180,400],0,'mixed_urban_riparian')]
terrain={'schema_version':9,'rivers':rivers,'crossings':crossings,'biomes':[{'id':id,'bounds_cm':[v*100 for v in box],'base_band_cm':z*100,'palette':pal} for id,box,z,pal in biomes],
 'height_policy':{'noise_amplitude_cm':150,'flatten_settlement_and_poi_pad':True,'max_authored_walkable_grade':0.25,'band_step_requires_ramp_or_stair':True,'settlement_flatten_apron_cm':1200,'noise_exclusion':'POI pads, roads, rivers, buildings, protected nav corridors'},
 'plateaus':[{'id':s['id']+'_plateau','center_cm':s['position_cm'],'radius_cm':s['footprint_radius_cm']+2500,'height_cm':s['position_cm'][2]} for s in settlements if s['position_cm'][2]>0],
 'forest_patches':[{'id':f'forest_{i:02d}','center_cm':at(x,y,z),'radius_cm':r*100,'scenery_tree_target':250,'mutable_tree_target':0} for i,(x,y,z,r) in enumerate([(-1050,230,0,100),(-650,400,0,110),(-970,-400,0,100),(-660,-1010,0,100),(-70,1020,8,100),(520,970,8,100),(1050,650,4,90),(1050,-610,4,100),(-500,670,0,90),(-600,-100,0,100),(200,-750,4,100),(530,450,4,90)])]}
terrain['heart_water_inset']={'id':'heart_water_inset','center_cm':at(15,10,4),'size_cm':[1200,600],'always_walkable':True,'wet':True,'static_substrate':True,'does_not_cover_claim_circle':True,'dry_alternative_poi_id':'heart_dry_flank','stage_id':'B90'}
write('terrain',terrain)

# Route centerlines form a strategic graph; native local navigation remains authoritative.
edges=[]
def edge(a,b,mode='road',via=None,width=800):
    points=[P[a]['position_cm']]+(via or [])+[P[b]['position_cm']]
    length=round(sum(dist(x,y) for x,y in zip(points,points[1:])))
    edges.append({'id':f'edge_{a}__{b}','from':a,'to':b,'kind':mode,'points_cm':points,'width_cm':width,'length_cm':length,'nominal_travel_s':round(length/420,2),'speed_bonus_percent':0,'bidirectional':True,'permanent_navigation':True,'stage_id':'B90'})
for a,b in [('highfort','stonewake'),('stonewake','scholars'),('scholars','bellwick'),('bellwick','ironworks'),('ironworks','ashcross'),('ashcross','reed_ford'),('reed_ford','reedbank'),('reedbank','riverport'),('riverport','oakmere'),('oakmere','highfort'),('highfort','millford'),('millford','north_bridge'),('millford','mill_bridge'),('north_bridge','stonewake'),('oakmere','mill_bridge'),('mill_bridge','capital'),('reedbank','south_bridge'),('south_bridge','capital_ford'),('capital_ford','capital'),('bellwick','east_bridge'),('east_bridge','capital'),('capital','heart_forge'),('heart_forge','ashcross'),('capital','boss_lair'),('boss_lair','stonewake'),('bellwick','east_ford'),('east_ford','scholars'),('capital','antechamber'),('antechamber','heart'),('antechamber','heart_dry_flank'),('heart_dry_flank','heart'),('capital','tower_a'),('capital','tower_b'),('capital','tower_c')]:edge(a,b)
for a,b in [('highfort','ruin_a'),('reed_ford','ruin_b'),('ironworks','ruin_c'),('highfort','north_pass'),('north_pass','stonewake'),('oakmere','west_pass'),('west_pass','highfort'),('ironworks','east_pass'),('east_pass','bellwick'),('ashcross','south_tunnel'),('south_tunnel','heart_forge'),('capital_ford','heart_dry_flank'),('east_bridge','tower_a'),('heart_forge','tower_c')]:edge(a,b,'trail',width=400)
connected={x[k] for x in edges for k in ['from','to']}
for p in pois:
    if p['id'] not in connected:
        nearby=min([q for q in pois if q['id'] in connected],key=lambda q:dist(p['position_cm'],q['position_cm']))
        edge(p['id'],nearby['id'],'district_lane' if p['kind']=='service' else 'trail',width=400)
        connected.add(p['id'])
# Explicit second approaches at rare targets and nonblocking escape branches.
for a,b in [('ruin_a','north_pass'),('ruin_b','ashcross'),('ruin_c','east_pass'),('boss_lair','fallen_garden'),('heart_forge','ironworks'),('heart','tower_b')]:edge(a,b,'trail',width=400)
def intersect(a,b,c,d):
    ax,ay=a[:2]; bx,by=b[:2];cx,cy=c[:2];dx,dy=d[:2]
    den=(bx-ax)*(dy-cy)-(by-ay)*(dx-cx)
    if abs(den)<1e-8:return None
    t=((cx-ax)*(dy-cy)-(cy-ay)*(dx-cx))/den
    u=((cx-ax)*(by-ay)-(cy-ay)*(bx-ax))/den
    if 0<=t<=1 and 0<=u<=1:return [round(ax+t*(bx-ax)),round(ay+t*(by-ay))]
    return None
for e in edges:
    contacts=[]
    for river in rivers:
        for a,b in zip(e['points_cm'],e['points_cm'][1:]):
            for c,d in zip(river['points_cm'],river['points_cm'][1:]):
                hit=intersect(a,b,c,d)
                if hit:
                    bridge=next((x['id'] for x in crossings if x['mode']=='permanent_bridge' and dist(hit,P[x['id']]['position_cm'])<3000),None)
                    contacts.append({'river_id':river['id'],'point_xy_cm':hit,'mode':'dry_bridge' if bridge else 'walkable_wet_ford','bridge_id':bridge})
    e['river_contacts']=contacts
    e['dry_for_entire_route']=all(x['mode']=='dry_bridge' for x in contacts)
write('routes',doc(edges))

population=[]
for role,total,purpose in [('civilian',96,'work_rest_trade'),('traveler',32,'visible_logistics'),('guard',24,'local_witness_response'),('official',20,'fixed_noncontender_service'),('hostile',24,'camp_or_wildlife'),('competitor',20,'ordinary_contender_progression')]:
    population.append({'role':role,'default_npc_count':total,'is_contender':role=='competitor','purpose':purpose,'new_kill_rewards':role in ['hostile','competitor'],'stage_id':'B92'})
write('population',{'schema_version':9,'roles':population,'baseline_npcs':216,'default_total_bodies':220,'noncontender_npcs':196,'contenders_total':24,'human_replaces_ai_contender':True,'hostile_split':{'camp_max_live':12,'boss':1,'ambient_wildlife':11},'ambient_wildlife_reward':0,'service_actors_are_components_or_shared_agents':True})

schedules=[]
for i in range(96):
    s=settlements[i%11]; home=next(b for b in buildings if b['settlement_id']==s['id'] and not b['enterable'])
    schedules.append({'id':f'civilian_{i:03d}','role':'civilian','home_building_id':home['id'],'home_public_anchor':s['id'],'work_poi_id':s['id']+'_workshop' if s['id'] in city_services else s['id'],'rest_poi_id':s['id'],'loop_s':600,'phase_offset_s':(i*37)%600,'segments':[{'start_s':0,'end_s':240,'state':'work'},{'start_s':240,'end_s':360,'state':'market_visit'},{'start_s':360,'end_s':480,'state':'rest'},{'start_s':480,'end_s':600,'state':'return_to_work'}],'threat_policy':'flee_to_local_guard_then_resume','wallet_initial_gold':3,'reward_budget':0})
for i in range(32):
    a=settlements[i%11]['id'];b=settlements[(i+3)%11]['id']
    schedules.append({'id':f'traveler_{i:03d}','role':'traveler','from_poi_id':a,'to_poi_id':b,'dwell_s':45,'phase_offset_s':i*11,'loop':'origin_to_destination_to_origin_by_route_graph','cargo':'visual_only_manifest_sealed_not_lootable_not_stock','wallet_initial_gold':0,'reward_budget':0,'threat_policy':'flee_then_resume'})
for i in range(24):
    city_id=settlements[i%11]['id'];s=city_id+'_guard' if city_id in city_services else city_id;target=settlements[(i+1)%11]['id'] if i>=20 else s
    schedules.append({'id':f'guard_{i:03d}','role':'guard','home_poi_id':s,'patrol_nodes':[s,target],'dwell_s':20,'local_response_radius_cm':3000,'reward_budget':0})
write('npc_schedules',doc(schedules))

object_types=[
 {'id':'tall_grass','count':96,'portable':False,'verbs':['enter','ignite'],'blocks_walk':False,'blocks_sight':False,'concealment':True,'burnable':True},
 {'id':'sapling','count':64,'portable':False,'verbs':['harvest','chop','ignite'],'blocks_walk':True,'blocks_sight':True,'concealment':False,'burnable':True},
 {'id':'door','count':72,'portable':False,'verbs':['open','close','lock','lockpick','force'],'blocks_walk':True,'blocks_sight':True,'burnable':False},
 {'id':'chest','count':48,'portable':False,'verbs':['open','lockpick','store','take'],'blocks_walk':False,'blocks_sight':False,'burnable':False},
 {'id':'crate','count':36,'portable':False,'verbs':['push','carry','place','open','break'],'blocks_walk':True,'blocks_sight':True,'burnable':False,'weight':12},
 {'id':'barrel','count':24,'portable':False,'verbs':['push','open','break'],'blocks_walk':True,'blocks_sight':False,'burnable':False,'weight':16},
 {'id':'water_flask','count':24,'portable':True,'verbs':['take','drop','trade','throw','pour'],'blocks_walk':False,'blocks_sight':False,'burnable':False,'inherits_item_id':'water_flask','weight':1},
 {'id':'oil_flask','count':24,'portable':True,'verbs':['take','drop','trade','throw','pour'],'blocks_walk':False,'blocks_sight':False,'burnable':False,'inherits_item_id':'oil_flask','weight':1},
 {'id':'torch','count':24,'portable':True,'verbs':['take','drop','trade','equip','ignite','extinguish','light_socket'],'blocks_walk':False,'blocks_sight':False,'burnable':False,'weight':2},
 {'id':'lantern','count':12,'portable':True,'verbs':['take','drop','trade','equip','toggle'],'blocks_walk':False,'blocks_sight':False,'burnable':False,'weight':3},
 {'id':'rope','count':12,'portable':True,'verbs':['take','drop','trade','attach','detach','use'],'blocks_walk':False,'blocks_sight':False,'burnable':False,'weight':3},
 {'id':'ladder','count':12,'portable':False,'verbs':['carry','place','use','retrieve'],'blocks_walk':False,'blocks_sight':False,'burnable':False,'weight':10},
 {'id':'furniture','count':32,'portable':False,'verbs':['inspect','use_storage'],'blocks_walk':True,'blocks_sight':False,'burnable':False}]
sizes={'tall_grass':[500,500,150],'sapling':[120,120,400],'door':[320,30,300],'chest':[100,80,80],'crate':[120,120,220],'barrel':[120,120,140],'water_flask':[20,20,30],'oil_flask':[20,20,30],'torch':[20,20,90],'lantern':[40,40,60],'rope':[40,40,30],'ladder':[70,500,30],'furniture':[200,100,100]}
for t in object_types:
    t['size_cm']=sizes[t['id']]
    t['interaction_radius_cm']=120
    t['concealment_shape']='disk_radius_250cm_feet_point' if t['id']=='tall_grass' else None
    t['proxy_shape']='upright_box_or_capsule_fitted_to_size_cm'
write('object_types',doc(object_types))
props=[]
enterable=[b for b in buildings if b['enterable']]
def prop(id,type,pos,**kw):
    r={'id':id,'type_id':type,'position_cm':pos,'initial_owner':'world','respawn':False,'stage_id':'B91'};r.update(kw);props.append(r);return r
for b in enterable:
    for side,dy in [('north',900),('south',-900)]:
        id=b['id']+'_door_'+side
        prop(id,'door',[b['position_cm'][0],b['position_cm'][1]+dy,b['position_cm'][2]],building_id=b['id'],hinge_side=side,initial_state='closed_unlocked',critical_path=False,lockable=side=='north',lock_owner='resident:'+b['settlement_id'] if side=='north' else None)
        b['door_ids'].append(id)
rng=random.Random(9001)
def blocked(p):
    return any(abs(p[0]-b['position_cm'][0])<b['size_cm'][0]/2+250 and abs(p[1]-b['position_cm'][1])<b['size_cm'][1]/2+250 for b in buildings)
for t in object_types:
    if t['id']=='door':continue
    for i in range(t['count']):
        base=pois[(i*7+len(t['id'])*3)%len(pois)]
        # Approved open pads are reconciled against native generation in B90/B91.
        for trial in range(500):
            ang=rng.random()*math.tau; r=rng.uniform(1800,4500)
            pos=[round(base['position_cm'][0]+r*math.cos(ang)),round(base['position_cm'][1]+r*math.sin(ang)),base['position_cm'][2]]
            if not blocked(pos) and max(abs(pos[0]),abs(pos[1]))<116000 and all(dist(pos,q['position_cm'])>250 for q in props if q['type_id']!='door') and dist(pos,P['heart']['position_cm'])>2000:break
        else:raise ValueError('no placement')
        extra={'anchor_poi_id':base['id'],'placement_pad_cm':250,'blocks_critical_corridor':False}
        if t['id']=='chest':
            extra['initial_contents']=[{'definition_id':'Fiber','count':2},{'definition_id':'Ore','count':1}] if i<12 else []
            extra['rare_objective_contents']=False
        if t['id']=='barrel':extra['payload']='empty_storage_no_combat_spill'
        if t['id']=='sapling':extra.update(harvest_output={'definition_id':'Fiber','count':1},removed_on_chop=True,hp=80,one_time_source_id=f'sapling_{i:03d}')
        prop(f"prop_{t['id']}_{i:03d}",t['id'],pos,**extra)
write('buildings',doc(buildings));write('props',doc(props))
anchors=[]
for i,b in enumerate([b for b in enterable if b['roof_access']]):
    anchors.append({'id':f'roof_anchor_{i:02d}','building_id':b['id'],'lower_cm':[b['position_cm'][0]+950,b['position_cm'][1],b['position_cm'][2]],'upper_cm':[b['position_cm'][0]+600,b['position_cm'][1],b['position_cm'][2]+400],'kind':'portable_ladder_or_rope','permanent_alternative':'interior_stairs','climb_ms':2000,'one_user_at_a_time':True,'capacity':1,'max_rise_cm':500,'link_bidirectional':True,'stage_id':'B91'})
write('traversal_anchors',doc(anchors))

full_econ=copy.deepcopy(econ)
full_econ.update(schemaVersion=9,package='Project Lockdown v0.9 full profile',date='2026-10-27')
full_econ['profile']={'id':'v09_full','contendersMin':24,'contendersMax':24,'seconds':5400,'levelCap':15,'skillSlots':6,'inventorySlots':24,'accessorySlots':4}
full_econ['markets']={}
legacy_presets=list(econ['markets'].values())
for i,city in enumerate(list(city_services)):
    market=copy.deepcopy(legacy_presets[i%3]);market['gold']=600
    for good,g in market['goods'].items():g['stock']*=3
    full_econ['markets'][city]=market
full_econ['market_poi_ids']={'capital':'capital_treasury','highfort':'highfort_market','scholars':'scholars_market','riverport':'riverport_market','ironworks':'ironworks_market'}
full_econ['contracts']['courier']['perContender']=20
full_econ['contracts']['courier']['paid_distance_band_cm']=[25000,80000]
full_econ['contracts']['courier']['minimum_distinct_endpoints']=True
full_econ['contracts']['courier']['proof']='server-issued sealed contract token; one delivery receipt; no ordinary item sale or duplicate credit'
full_econ['contracts']['courier'].update(active_limit=1,issuance_limit=40,quota_commit='successful_delivery',death_policy='token_drops_same_owner_can_recover_no_free_replacement',distance_metric='shortest_strategic_graph_cm')
full_econ['npcTicks']['times']=list(range(180,5400,180))
full_econ['npcTicks']['production']=[{'market':'riverport','good':'Fiber','count':1,'goldBurn':2},{'market':'ironworks','good':'Ore','count':1,'goldBurn':3}]
full_econ['npcTicks']['consumption']=[{'market':'riverport','good':'Fiber','count':1},{'market':'ironworks','good':'Ore','count':1},{'market':'capital','good':'Toolkit','count':1}]
full_econ['shortage'].update(start=1800,end=1980,market='ironworks')
full_econ['bank']['location']='capital_treasury';full_econ['bank']['access_poi_ids']=['capital_treasury','highfort_market','scholars_market','riverport_market','ironworks_market']
full_econ['bank']['shared_per_contender_account']=True
full_econ['charter']['activation_poi_id']='antechamber';full_econ['charter']['purchase_poi_id']='capital_treasury'
full_econ['charter'].pop('contestRangeCm',None)
full_econ['charter']['claim_cancellation']='v09 inherits v08 actual control/displacement; no ordinary damage or proximity cancellation'
full_econ['gather']={'chargesPerNode':6,'nodes':9,'basicSeconds':6,'rangeCm':120,'xp':0,'resetOnRespawn':False}
full_econ['accounting']={'maxRewardMints':'790*N+1114','initialMarketGold':3000,'initialResidentGold':288,'initialTotalGold':3288,'maxProducerBurn':145,'goldIdentity':'end=start+minted-burned','worldRewardGold':1074,'contractRewardGoldPerContender':790,'shortageRewardGold':40,'N':24,'rewardMintUpperBound':20074,'totalGoldBeforeBurnUpperBound':23362}
full_econ['feasibility']['status']='inherited arithmetic only; use v09 native travel/current quotes; not guaranteed starting market quote'
full_econ['deferred']=['loans_interest','general_dialogue','free_physics','fully_physical_caravan_supply_chains','human_60_90_minute_balance_evidence']
write('economy',full_econ)

sources=[]
for i in range(12):sources.append({'id':f'camp_{i+1:02d}','poi_id':f'camp_{i+1:02d}','kind':'camp','spawn_id_count':6,'first_at_ms':0,'spawn_gap_min_ms':600000,'max_live':1,'gold_each':12,'xp_each':90,'replenishes':False,'source_identity':'match+camp+ordinal','stage_id':'B92'})
for i,id in enumerate(['ruin_a','ruin_b','ruin_c']):sources.append({'id':id,'poi_id':id,'kind':'ruin_cache','unique_contents':[f'ancient_key_{chr(97+i)}','arcane_catalyst',['book_temporal_lock','book_grand_exchange','book_gate_weave'][i]],'copies':1,'xp_first_discovery_per_contender':50,'replenishes':False,'stage_id':'B93'})
sources.append({'id':'boss_once','poi_id':'boss_lair','kind':'boss','gold_each':90,'xp_each':250,'copies':1,'unique_contents':['heart_fragment','book_sovereign_ward'],'replenishes':False,'stage_id':'B93'})
for id in ['tower_a','tower_b','tower_c']:sources.append({'id':id,'poi_id':id,'kind':'tower','gold_each':40,'xp_each':150,'copies':1,'unique_contents':['tower_relic'],'replenishes':False,'stage_id':'B93'})
for id,name,x,y,z,good in resources:sources.append({'id':id,'poi_id':id,'kind':'resource','definition_id':good,'charges':6,'units_each':1,'xp':0,'gold':0,'replenishes':False,'stage_id':'B92'})
write('reward_sources',doc(sources))
write('legacy_aliases',{'schema_version':9,'aliases':{'City':'capital','CityTreasury':'capital_treasury','WestTown':'riverport','EastTown':'ironworks','WildCrossing':'capital_ford','RuinA':'ruin_a','RuinB':'ruin_b','RuinC':'ruin_c','BossLair':'boss_lair','HeartForge':'heart_forge','CentralPrecinct':'heart','GrandTowerA':'tower_a','GrandTowerB':'tower_b','GrandTowerC':'tower_c','HeartAntechamber':'antechamber','OfficialSupply':'capital_supply'},'semantic_policy':'aliases adapt inherited contracts; never spawn both old and new actors'})
write('stages',doc([
 {'id':'B90','name':'Full-world skeleton and spatial import','depends_on':[],'exit_gate':'all 96 POIs connected by native permanent routes; five cities and six towns; measured travel'},
 {'id':'B91','name':'Functional visibility, interiors and objects','depends_on':['B90'],'exit_gate':'36/144 interiors; 480 prop instances; mutation, slot and navigation cases pass'},
 {'id':'B92','name':'Services, bounded economy and living population','depends_on':['B91'],'exit_gate':'196 noncontender NPC roles plus locked 24 contender roster; conserved stock/gold; sensory AI'},
 {'id':'B93','name':'Four victory routes and capital convergence','depends_on':['B92'],'exit_gate':'three towers, single boss, three unique keys; every route reaches physical claim; endpoint tests'},
 {'id':'B94','name':'Full-match, multiplayer and performance validation','depends_on':['B93'],'exit_gate':'60/75/90-minute native runs, human tests and telemetry; no balance or performance claims without evidence'}]))

write('world_objects_rules',{'schema_version':9,'transaction_radius_cm':120,'quiet_s':6,'ground_pickup_ms':500,'harvest_ms':4000,'heavy_carry_ms':1000,'placement_ms':1500,'door_open_ms':300,'door_force_ms':3000,'door_lockpick_ms':3000,'torch_ignite_socket_ms':1000,'interaction_classes':{'world':'door, pickup, harvest, heavy carry, fixed workstation','item':'portable torch/lantern/flask/rope activation'},'carry':{'one_heavy_prop':True,'hands_suppressed':True,'speed_percent':70,'attack_skill_item_service_during_carry':False,'drop_on_damage_death_or_ability_request':True,'weight_check':'heavy weight <= natural capacity; no bag instance while carried'},'placement':{'critical_corridor_exclusion_cm':400,'reject_occupied_capsules':True,'no_stack':True,'valid_surface_grade_max':0.25,'minimum_clear_walk_width_cm':300},'torch':{'equipment_slot':'accessory','fuel_ms':180000,'refill':False,'light_cosmetic_radius_cm':600,'sight_bonus_cm':0,'surface_max_cells':4,'burning_ms':3000,'cooldown_ms':12000,'direct_unit_damage':0,'active_blocked_by':'Mute','ground_emitter_cannot_be_remotely_activated':True},'lantern':{'equipment_slot':'accessory','fuel_ms':300000,'refill':False,'light_cosmetic_radius_cm':800,'sight_bonus_cm':0,'active_blocked_by':'Mute'},'rope':{'equipment_slot':'accessory','consume_on_attach':False,'action_requires_binding':True,'detach_quiet_s':6,'max_rise_cm':500,'use_placed_link':'world_channel_not_free_portable_cast'},'sapling':{'burning_clears_proxy_after_ms':3000,'harvest_fiber':1,'destruction_fiber':0,'regen':False,'static_tree_fire_spread':False},'grass':{'hide_requires_inside_and_no_detected_authorized_observer':True,'reveal_on_attack_or_cast':True,'burning_disables_concealment_ms':3000,'burnt_for_match':True,'concealment_rearm_ms':1000,'reveal_after_offense_ms':2000,'proximity_reveal_cm':200},'door':{'initial_locked':False,'relock_owner_only':True,'generic_lockpick_no_skill_book':True,'entry_always_has_force_or_alternative':True,'force_hp_cost':0,'official_service_front_doors_lockable':False,'north_private_door_cannot_block_south_public_entry':True,'occupancy_blocks_close':True,'authored_destruction_only':True}})
write('security',{'schema_version':9,'civic_zone_radius_cm':{'capital':12000,'city':7000,'town':3000},'heart_contested_override_radius_cm':7000,'witness_geometry_required':True,'incident_expiry_ms':120000,'alarm_range_cm':3000,'guard_leash_from_home_cm':6000,'lose_sight_ms':5000,'search_ms':5000,'civilian_resume_safe_ms':10000,'guard':{'count':24,'hp':600,'armor':10,'magic_resist':0.15,'attack_damage':35,'attack_interval_ms':1500,'attack_range_cm':220,'movement_cm_s':420,'sight_cm':1800,'hard_control':False,'respawn_ms':90000,'reward_gold':0,'reward_xp':0,'loot':[],'spawn_protection_ms':3000},'no_fines_prison_bounty_or_reinforcement_mint':True,'stage_id':'B92'})
write('map_items',doc([{'id':id,'name':id.title(),'equipment_slot':'accessory','weight':w,'consumable':False,'tradable':True,'quality':'standard_only','stats':{},'passives':{},'source':'finite_world_endowment','official_price_gold':None,'death_policy':'drop','active':{'op':'portable_world_tool','verbs':verbs,'binding':'accessory','blocked_by':'Mute','mana_cost':0,'range_cm':120},'persistent_instance_fields':['fuel_remaining_ms','lit','location','cooldown_ready_at','revision'],'stage_id':'B91'} for id,w,verbs in [('torch',2,['toggle','ignite_socket']),('lantern',3,['toggle']),('rope',3,['attach_to_anchor'])]]))
write('map_operations',doc([
 {'id':'portable_world_tool','category':'item','meaning':'Bound torch/lantern/rope action only. Dispatch by exact tool+verb, preserve fuel/identity, reserve target/field before commit; reject unknown verb.'},
 {'id':'world_transfer','category':'world','meaning':'Take/drop/container-store one actual instance; capacity and ownership atomic; never clone'},
 {'id':'world_carry_place','category':'world','meaning':'Bind one heavy world instance to carrier then fitted ground or approved anchor; suppress actions and reserve clear placement'},
 {'id':'world_push','category':'world','meaning':'Sweep heavy prop <=150 cm over 1000 ms; fit permanent clearance; no body displacement'},
 {'id':'world_door','category':'world','meaning':'Open/close/lock/force rear or public door under documented owner rules; collision+sight+navigation revision atomic'},
 {'id':'world_harvest','category':'world','meaning':'Finite source charge reservation, quiet channel, exact yield+capacity commit; one source id can never reset'},
 {'id':'world_traverse_anchor','category':'world','meaning':'One reserved 2000 ms link use, root/control/life checks; interruption restores last valid endpoint'},
 {'id':'world_break_container','category':'world','meaning':'Quiet 3000 ms break; existing contents transfer to world once; no material or payload mint'}]))
print(json.dumps({'pois':len(pois),'buildings':len(buildings),'interiors':len(enterable),'props':len(props),'routes':len(edges),'traversal_anchors':len(anchors),'npc_schedule_rows':len(schedules),'stage_ids':['B90','B91','B92','B93','B94']}))

write('spawn_pads',doc([{'id':f'spawn_{i:02d}','settlement_id':settlements[1+i%4]['id'],'position_cm':[settlements[1+i%4]['position_cm'][0]+(i//4-2)*800,settlements[1+i%4]['position_cm'][1],settlements[1+i%4]['position_cm'][2]],'contender_slot':i,'protected_ms':3000,'respawn_policy':'assigned_home_city; nearest_clear pad in same city; never enemy proximity oracle','stage_id':'B92'} for i in range(24)]))
write('contender_ai',{'schema_version':9,'default_count':20,'roster_total':24,'decisions_per_second_near':2,'macro_decision_interval_ms':5000,'goal_weights':{'courier_and_supply':4,'combat_camps':3,'keys':1,'artifact':1,'siege':1},'policy':[{'state':'establish','condition':'needs resources or level','actions':['choose known payable work','buy legal kit','train only with actual money and slots']},{'state':'progress','condition':'ordinary needs met','actions':['choose feasible route goal','navigate through known static graph','react only to observed hazards']},{'state':'respond','condition':'public warning or visible rival threat','actions':['estimate own travel','defend if arrival feasible','otherwise continue local progression']},{'state':'contest','condition':'valid access and near Heart','actions':['claim via ordinary hold command','interrupt observed rival via legal active']}],'quiet_civilian_behavior_is_not_contender_ai':True,'knowledge_policy':'owned+observed+intentionally public; no human label, private wallet, remote dropped-key coordinates or hidden hazard weights','route_fallback':'blocked planned action replans; no free money, teleport or forced objective removal','stage_id':'B92'})
write('simulation_budgets',{'schema_version':9,'near_cm':10000,'mid_cm':30000,'near_perception_hz':5,'near_decision_hz':2,'mid_decision_hz':1,'far_schedule_hz':0.2,'combat_hz':'reuse native fixed authority; never coarse-resolve player-observable combat','wake_triggers':['any authorized player sight volume','combat projectile or hazard','public objective interaction','guard pursuit','pending inventory or institutional transaction'],'far_unit_policy':'persist identity and route progress; no disappearing body across any observer; no far-vs-near duplicate receipts','target_client_fps':60,'target_server_tick_hz':30,'target_server_p95_tick_ms':33.3,'target_server_p99_tick_ms':50,'max_interaction_roundtrip_p95_ms':250,'max_high_level_route_query_p95_ms':10,'performance_status':'targets requiring recorded hardware, build, density and network conditions; unmeasured','stage_id':'B94'})

write('official_assignments',doc([{'id':f'official_{i:02d}','poi_id':[s['poi_id'] for s in service_rows if s['poi_id'].startswith(list(city_services)[i//4]+'_')][i%4],'protected_official':True,'routine':'fixed_counter_idle_and_work_no_pursuit','stage_id':'B92'} for i in range(20)]))
