"""Repair the Tripo garment openings without changing the rig or bind pose."""
import sys,json,collections,argparse
from pathlib import Path
import numpy as np
from scipy.sparse import coo_matrix
from scipy.sparse.csgraph import connected_components
from fbx_io import read,write,child

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source',type=Path,required=True)
parser.add_argument('--reference',type=Path,required=True)
parser.add_argument('--output',type=Path,required=True)
parser.add_argument('--diagnostics',type=Path)
args=parser.parse_args()
src=args.source
if args.diagnostics:args.diagnostics.mkdir(parents=True,exist_ok=True)
d=read(src);objects=next(n for n in d['nodes'] if n['name']=='Objects')['children']
geo=next(n for n in objects if n['name']=='Geometry')
v=np.array(child(geo,'Vertices')['props'][0]).reshape(-1,3)
pi=np.array(child(geo,'PolygonVertexIndex')['props'][0]);f=np.where(pi<0,-pi-1,pi).reshape(-1,3)
nl=child(geo,'LayerElementNormal');normals=np.array(child(nl,'Normals')['props'][0]).reshape(-1,3)[np.array(child(nl,'NormalsIndex')['props'][0])].reshape(-1,3,3)
ul=child(geo,'LayerElementUV');uv=np.array(child(ul,'UV')['props'][0]).reshape(-1,2)[np.array(child(ul,'UVIndex')['props'][0])].reshape(-1,3,2)
unique,inv=np.unique(np.round(v,8),axis=0,return_inverse=True);wf=inv[f]
ed=np.concatenate([wf[:,[0,1]],wf[:,[1,2]],wf[:,[2,0]]]);adj=coo_matrix((np.ones(len(ed)*2),(np.r_[ed[:,0],ed[:,1]],np.r_[ed[:,1],ed[:,0]])),shape=(len(unique),len(unique))).tocsr()
_,lab=connected_components(adj);vl=lab[inv];fl=vl[f[:,0]]
clusters=[o for o in objects if o['name']=='Deformer' and o['props'][2]=='Cluster']
bone=[o['props'][1].split('\x00')[0] for o in clusters];bone=[n.split('::')[-1] for n in bone]
weights=np.zeros((len(v),len(bone)))
for k,c in enumerate(clusters):
 ix=next((x for x in c['children'] if x['name']=='Indexes'),None)
 if ix is not None:weights[np.array(ix['props'][0]),k]=child(c,'Weights')['props'][0]
assert all(n in bone for n in ['head','pelvis','spine_01','spine_02','spine_03','spine_04'])
report={'original_vertices':len(v),'original_triangles':len(f),'bones':len(bone)}

# The head is intentionally left unchanged for the replacement-head workflow.
original_weights=weights.copy()
original_vertices=v.copy()

# Only reverse triangles whose authored normals oppose their winding.
cross=np.cross(v[f[:,1]]-v[f[:,0]],v[f[:,2]]-v[f[:,0]])
area=np.linalg.norm(cross,axis=1)
dot=np.sum(cross/np.maximum(area[:,None],1e-15)*normals.mean(1),axis=1)
flip=(dot<-.1)&(v[f,:,][...,2].max(1)<.865)
f[flip]=f[flip][:,[0,2,1]];uv[flip]=uv[flip][:,[0,2,1]];normals[flip]=normals[flip][:,[0,2,1]]
keep=area>2e-12
report.update(reversed_triangles=int(flip.sum()),degenerate_triangles_removed=int((~keep).sum()))
f=f[keep];uv=uv[keep];normals=normals[keep];fl=fl[keep]

verts=v.tolist();faces=f.tolist();uvs=uv.tolist();normal_list=normals.tolist();wts=weights.tolist();labels=fl.tolist()
vuv=np.zeros((len(v),2));cnt=np.zeros(len(v))
np.add.at(vuv,f.reshape(-1),uv.reshape(-1,2));np.add.at(cnt,f.reshape(-1),1);vuv/=np.maximum(cnt[:,None],1)

def add_vertex(p,w):
 i=len(verts);verts.append(np.array(p).tolist());w=np.array(w);w/=max(w.sum(),1e-12);wts.append(w.tolist());return i
def add_tri(tri,tx,label):
 faces.append(list(tri));uvs.append(np.array(tx).tolist());normal_list.append([[0.,0.,0.]]*3);labels.append(label)
def boundary_edges(faceids):
 fs=np.array([faces[i] for i in faceids]);edges=np.concatenate([fs[:,[0,1]],fs[:,[1,2]],fs[:,[2,0]]])
 _,inverse,count=np.unique(np.sort(edges,axis=1),axis=0,return_inverse=True,return_counts=True)
 return edges[count[inverse]==1]
def ring_for(component,zlo,zhi):
 edges=boundary_edges([i for i,l in enumerate(labels) if l==component]);p=np.array(verts)
 eligible=[(int(a),int(b)) for a,b in edges if zlo<p[a,2]<zhi and zlo<p[b,2]<zhi]
 # The chosen hem/cut is a simple closed loop with no branches.
 out={a:b for a,b in eligible};assert len(out)==len(eligible)
 a=eligible[0][0];ring=[a];b=out[a]
 while b!=a:
  assert b not in ring;ring.append(b);b=out[b]
 assert len(ring)==len(eligible),(component,len(ring),len(eligible))
 return ring

refs=json.loads(args.reference.read_text())['refs']
spine=['pelvis','spine_01','spine_02','spine_03','spine_04','spine_05']
height=np.array([refs[n][0][2]/182.314529 for n in spine])
def torso_weight(z):
 result=np.zeros(len(bone))
 if z<=height[0]:result[bone.index(spine[0])]=1;return result
 if z>=height[-1]:result[bone.index(spine[-1])]=1;return result
 j=np.searchsorted(height,z);t=(z-height[j-1])/(height[j]-height[j-1])
 result[bone.index(spine[j-1])]=1-t;result[bone.index(spine[j])]=t;return result

def extrude_ring(top,steps,position,weightfn,label,cap=False):
 previous=top;top_positions=np.array([verts[i] for i in top]);top_weights=np.array([wts[i] for i in top]);topuv=vuv[top]
 for step in range(1,steps+1):
  t=step/steps;current=[];tx=topuv+np.array([0,.004*t])
  for j,p in enumerate(top_positions):
   q=position(p,t);current.append(add_vertex(q,weightfn(q,top_weights[j],t)))
  oldtx=topuv+np.array([0,.004*(step-1)/steps])
  for j,a in enumerate(previous):
   k=(j+1)%len(top);b=previous[k];an=current[j];bn=current[k]
   add_tri([b,a,an],[oldtx[k],oldtx[j],tx[j]],label);add_tri([b,an,bn],[oldtx[k],tx[j],tx[k]],label)
  previous=current
 if cap:
  p=np.array([verts[i] for i in previous]).mean(0);w=np.array([wts[i] for i in previous]).mean(0);center=add_vertex(p,w)
  for j,a in enumerate(previous):
   k=(j+1)%len(top);add_tri([previous[k],a,center],[tx[k],tx[j],tx.mean(0)],label)
 return previous

# Close the hollow torso beneath the chest. Every new row shares skin weights
# along the spine, and the first row shares the existing boundary vertices.
chest=ring_for(0,.72,.74);report['chest_boundary_vertices']=len(chest)
def torso_pos(p,t):
 q=p.copy();q[2]=(1-t)*p[2]+t*.548
 radial=1-.10*np.sin(t*np.pi/2);q[0]*=radial;q[1]=.004+(p[1]-.004)*(1-.14*t)
 return q
def torso_w(q,w,t):
 blend=min(t*4,1);return (1-blend)*w+blend*torso_weight(q[2])
extrude_ring(chest,10,torso_pos,torso_w,0,True)

# A tucked extension gives the jersey and jacket an overlap during bending.
hem=ring_for(14,.59,.64);report['hem_boundary_vertices']=len(hem)
def hem_pos(p,t):
 q=p.copy();q[0]*=1-.10*t;q[1]=.003+(q[1]-.003)*(1-.10*t);q[2]-=.070*t;return q
def hem_w(q,w,t):
 return (1-t)*w+t*torso_weight(q[2])
extrude_ring(hem,5,hem_pos,hem_w,14)

def vertex_normals(points,triangles):
 points=np.array(points);triangles=np.array(triangles);n=np.zeros_like(points)
 a=points[triangles[:,1]]-points[triangles[:,0]];b=points[triangles[:,2]]-points[triangles[:,0]]
 fn=np.cross(a,b);fn/=np.maximum(np.linalg.norm(fn,axis=1)[:,None],1e-15)
 for j in range(3):
  a=points[triangles[:,(j+1)%3]]-points[triangles[:,j]];b=points[triangles[:,(j+2)%3]]-points[triangles[:,j]]
  angle=np.arctan2(np.linalg.norm(np.cross(a,b),axis=1),np.sum(a*b,axis=1))
  np.add.at(n,triangles[:,j],fn*angle[:,None])
 n/=np.maximum(np.linalg.norm(n,axis=1)[:,None],1e-15);return n

# Build a closed, ~1.5 mm lining on the jersey only, including all armhole rims.
shirt_faces=[i for i,l in enumerate(labels) if l==14];shirt=np.array([faces[i] for i in shirt_faces]);shirt_v=np.unique(shirt)
sn=vertex_normals(verts,shirt);rim=boundary_edges(shirt_faces);inner={}
for i in shirt_v:inner[int(i)]=add_vertex(np.array(verts[i])-.0008*sn[i],wts[i])
for i in shirt_faces:
 a,b,c=faces[i];t=uvs[i];add_tri([inner[a],inner[c],inner[b]],[t[0],t[2],t[1]],114)
for a,b in rim:
 ta=vuv[a] if a<len(vuv) else np.array([.5,.5]);tb=vuv[b] if b<len(vuv) else np.array([.501,.5]);off=np.array([0,.0002])
 add_tri([b,a,inner[a]],[tb,ta,ta+off],115);add_tri([b,inner[a],inner[b]],[tb,ta+off,tb+off],115)
report['shirt_rim_edges_closed']=len(rim)

# Recalculate smooth normals on repaired skin/shell surfaces; retain other parts.
p=np.array(verts);tri=np.array(faces);labs=np.array(labels);nn=np.array(normal_list)
for group in [0,14,114,115]:
 ids=np.where(labs==group)[0];vn=vertex_normals(p,tri[ids]);update=ids[p[tri[ids],2].max(1)<.865] if group==0 else ids;nn[update]=vn[tri[update]]
for i in np.where(np.linalg.norm(nn,axis=2).min(1)<.5)[0]:
 n=np.cross(p[tri[i,1]]-p[tri[i,0]],p[tri[i,2]]-p[tri[i,0]]);n/=max(np.linalg.norm(n),1e-15);nn[i]=n

child(geo,'Vertices')['props'][0]=p.reshape(-1).tolist()
pi=tri.copy();pi[:,-1]=-pi[:,-1]-1;child(geo,'PolygonVertexIndex')['props'][0]=pi.reshape(-1).tolist()
geo['children']=[n for n in geo['children'] if n['name']!='Edges']
child(child(geo,'LayerElementSmoothing'),'Smoothing')['props'][0]=[1]*len(tri)
child(nl,'Normals')['props'][0]=nn.reshape(-1).tolist();child(nl,'NormalsIndex')['props'][0]=list(range(len(tri)*3))
child(ul,'UV')['props'][0]=np.array(uvs).reshape(-1).tolist();child(ul,'UVIndex')['props'][0]=list(range(len(tri)*3))
ww=np.array(wts);ww[ww<1e-6]=0;ww/=ww.sum(1)[:,None]
for j,c in enumerate(clusters):
 ix=next((x for x in c['children'] if x['name']=='Indexes'),None)
 if ix is None:assert not np.any(ww[:,j]);continue
 ids=np.where(ww[:,j]>0)[0];ix['props'][0]=ids.tolist();child(c,'Weights')['props'][0]=ww[ids,j].tolist()
args.output.parent.mkdir(parents=True,exist_ok=True);write(d,args.output)
report.update(vertices=len(p),triangles=len(tri),max_weight_sum_error=float(np.max(abs(ww.sum(1)-1))),bounds=[p.min(0).tolist(),p.max(0).tolist()])
report['torso_remaining_cut_edges']=len([e for e in boundary_edges(np.where(labs==0)[0]) if np.array(verts)[e,2].max()<.74])
report['jersey_open_edges']=len(boundary_edges(np.where(np.isin(labs,[14,114,115]))[0]))
assert report['torso_remaining_cut_edges']==0 and report['jersey_open_edges']==0,report
assert np.isfinite(p).all() and np.isfinite(ww).all() and np.isfinite(nn).all()
head=np.where(original_vertices[:,2]>=.865)[0]
assert np.array_equal(p[head],original_vertices[head])
assert np.allclose(ww[head],original_weights[head]/original_weights[head].sum(1)[:,None],atol=1e-6)
report['head_geometry_and_weights_unchanged']=True
if args.diagnostics:
 np.savez(args.diagnostics/'garments.npz',v=p,f=tri,labels=labs,normals=nn,weights=ww)
 (args.diagnostics/'garment_report.json').write_text(json.dumps(report,indent=2))
print(json.dumps(report,indent=2))
