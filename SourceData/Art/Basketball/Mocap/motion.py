import sys,json,math,bisect
from pathlib import Path
import numpy as np
from scipy.spatial.transform import Rotation as R, Slerp
from PIL import Image, ImageDraw
from inspect_fbx import read_fbx
ROOT=Path(__file__).parent
def unit(v):return v/max(np.linalg.norm(v),1e-8)
class Motion:
 def __init__(self,path):
  data=read_fbx(path);objs=next(x for x in data if x['name']=='Objects')['children']
  self.objs={o['props'][0]:o for o in objs};models={i:o for i,o in self.objs.items() if o['name']=='Model'}
  self.ids={i:o['props'][1].split('\x00')[0] for i,o in models.items() if o['props'][2]=='LimbNode'}
  self.props={self.ids[i]:{p['props'][0]:p['props'][4:] for c in o['children'] if c['name']=='Properties70' for p in c['children']} for i,o in models.items() if i in self.ids}
  cons=[c['props'] for c in next(x for x in data if x['name']=='Connections')['children']]
  self.parents={self.ids[c[1]]:self.ids.get(c[2]) for c in cons if c[0]=='OO' and c[1] in self.ids}
  self.curves={};self.end=0
  for c in cons:
   if c[0]!='OP' or c[2] not in self.ids:continue
   for a in cons:
    if a[0]=='OP' and a[2]==c[1]:
     ch={v['name']:v['props'] for v in self.objs[a[1]]['children']}
     times=np.array(ch['KeyTime'][0],float)/46186158000
     vals=np.array(ch['KeyValueFloat'][0]);self.end=max(self.end,times[-1])
     self.curves[self.ids[c[2]],c[3],a[3][-1]]=(times,vals)
  self.names=list(self.props)
 def sample(self,t):
  out={}
  for n in self.names:
   p=self.props[n];l={}
   for attr,default in [('Lcl Translation',[0,0,0]),('Lcl Rotation',[0,0,0])]:
    v=list(p.get(attr,default))
    for i,axis in enumerate('XYZ'):
     if (n,attr,axis) in self.curves:
      times,vals=self.curves[n,attr,axis];v[i]=float(np.interp(t,times,vals))
    l[attr]=v
   pos=np.array(l['Lcl Translation']);rot=R.from_euler('xyz',l['Lcl Rotation'],degrees=True)
   par=self.parents[n]
   if par:pos=out[par][0]+out[par][1].apply(pos);rot=out[par][1]*rot
   out[n]=(pos,rot)
  return out
def sheet(m,output,times):
 w=240;h=330;img=Image.new('RGB',(w*6,h*math.ceil(len(times)/6)),(24,30,39));d=ImageDraw.Draw(img)
 for i,t in enumerate(times):
  pose=m.sample(t);hip=pose['hip'][0]
  # View from front-right; retain body height and root-centre horizontally.
  def pt(p):
   p=p.copy();p[:2]-=hip[:2];return (i%6*w+120+(p[0]*.8-p[1]*.6)*1.2,i//6*h+290-p[2]*1.2)
  for n,v in pose.items():
   p=v[0];par=m.parents.get(n)
   if par in pose and not any(x in n.lower() for x in ['thumb','index','mid','ring','pinky','eye','twist','ik_','corrective']):d.line([pt(p),pt(pose[par][0])],fill=(247,156,52) if n.startswith('r') or n.endswith('_r') else (110,196,232),width=4)
  d.text((i%6*w+12,i//6*h+10),f'{t:.2f} s',fill='white')
 img.save(output)
