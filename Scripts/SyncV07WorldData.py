"""Stage adopted world rules and the authored B76 native regional extension."""
from pathlib import Path
import json,hashlib,argparse,copy
r=Path(__file__).resolve().parents[1];p=argparse.ArgumentParser();p.add_argument('--check',action='store_true');a=p.parse_args()
world=json.loads((r/'SourceData/Catalogs/V07/world_and_routes.json').read_text(encoding='utf-8-sig'));rules=json.loads((r/'SourceData/Catalogs/V07/v07_rules.json').read_text(encoding='utf-8-sig'))
layout={'RuinA':[-3000,1000,0],'RuinB':[0,2300,0],'RuinC':[3000,0,0],'BossLair':[2200,2700,0],'HeartForge':[2500,1400,0],'HeartAntechamber':[0,-500,0],'CampA':[-2500,2200,0],'CampB':[4800,2500,0],'CampC':[4800,-2800,0],'ShrineWest':[-3000,-700,0],'ShrineEast':[3000,1500,0],'GrandTowerA':[-650,300,0],'GrandTowerB':[650,300,0],'GrandTowerC':[0,850,0]}
payload={'schema_version':7,'world':world,'rewards':rules['world_rewards'],'layout':layout}
raw=(json.dumps(payload,indent=2)+'\n').encode();digest=hashlib.sha256(raw).hexdigest()
base=json.loads((r/'Config/RegionLayout.json').read_text(encoding='utf-8-sig'))['regions'][:2];region=copy.deepcopy(base[0]);region['name']='B76 Civic Reach';node={n['name']:n for n in region['nodes']};node['City']['position']=region['guild'];
for route in region['routes']:
 if route['from']==region['cityId']:route['points'][0]=region['guild']
 if route['to']==region['cityId']:route['points'][-1]=region['guild']
extra=['RuinA','RuinB','RuinC','BossLair','HeartForge','CentralPrecinct','HeartAntechamber']
for i,name in enumerate(extra,180):
 n={'id':i,'name':name,'site':0,'position':region['heart'] if name=='CentralPrecinct' else layout[name],'radius':200,'visit':False};region['nodes'].append(n);node[name]=n
for left,right in world['required_edges']:
 if any({z['from'],z['to']}=={node[left]['id'],node[right]['id']} for z in region['routes']):continue
 region['routes'].append({'from':node[left]['id'],'to':node[right]['id'],'covered':False,'points':[node[left]['position'],node[right]['position']]})
region['routes'].append({'from':node['HeartAntechamber']['id'],'to':node['CentralPrecinct']['id'],'covered':True,'points':[layout['HeartAntechamber'],[-430,-500,0],[-430,0,0],region['heart']]})
for name,pos,size in [('PrecinctWestCover',[-320,300,110],[40,400,220]),('PrecinctEastCover',[320,300,110],[40,400,220]),('PrecinctNorthCover',[0,500,110],[400,40,220])]:region['geometry'].append({'name':name,'position':pos,'size':size,'mesh':'Cube','floor':False,'high':False,'pitch':0})
for i,name in enumerate(['A','B','C']):
 pos=layout['GrandTower'+name];region['geometry'].append({'name':'TowerDoor'+name,'position':[pos[0]+130,pos[1],100],'size':[40,180,200],'mesh':'Cube','floor':False,'high':False,'pitch':0})
outputs=[(r/'Content/Lockdown/Data/V07/world.json',raw),(r/'Source/Lockdown/Private/World/LDWorldContract.h',('#pragma once\n#define LD_WORLD_SHA TEXT("'+digest+'")\n').encode()),(r/'Config/V07RegionLayout.json',(json.dumps({'regions':[region]},indent=2)+'\n').encode())]
for path,data in outputs:
 if a.check:assert path.read_bytes().replace(b'\r\n',b'\n')==data,path
 else:path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)
print('PASS B76 world contract '+digest)
