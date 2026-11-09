from motion import *
import copy
T=json.loads((ROOT/'target.json').read_text());names=T['names'];parents=T['parents'];refs=T['refs']
refp={n:np.array(t[0]) for n,t in refs.items()};refq={n:R.from_quat(t[1]) for n,t in refs.items()}
localref={}
for n in names:
 p=parents[n]
 if p in refs:localref[n]=[(refq[p].inv().apply(refp[n]-refp[p])/np.array(refs[p][2])).tolist(),(refq[p].inv()*refq[n]).as_quat().tolist(),(np.array(refs[n][2])/refs[p][2]).tolist()]
 else:localref[n]=copy.deepcopy(refs[n])
def fk(local):
 out={}
 for n in names:
  p,q,s=local[n];p=np.array(p);q=R.from_quat(q);s=np.array(s);par=parents[n]
  if par in out:
   pp,pq,ps=out[par];p=pp+pq.apply(p*ps);q=pq*q;s=ps*s
  out[n]=(p,q,s)
 return out
def setrot(local,n,q):
 g=fk(local);p=parents[n];local[n][1]=((g[p][1].inv()*q) if p in g else q).as_quat().tolist()
def between(a,b):
 a=unit(a);b=unit(b);v=np.cross(a,b);w=1+np.dot(a,b)
 if w<1e-6:return R.from_rotvec(unit(np.cross(a,[0,1,0]))*np.pi)
 return R.from_quat(np.r_[v,w]/np.linalg.norm(np.r_[v,w]))
def solve(local,upper,lower,end,target,pole):
 g=fk(local);s,e,h=[g[n][0] for n in [upper,lower,end]];l1=np.linalg.norm(e-s);l2=np.linalg.norm(h-e)
 delta=target-s;d=min(np.linalg.norm(delta),l1+l2-.05);direction=unit(delta);target=s+direction*d
 a=(l1*l1-l2*l2+d*d)/(2*d);bend=unit(pole-s-direction*np.dot(pole-s,direction));elbow=s+direction*a+bend*np.sqrt(max(0,l1*l1-a*a))
 setrot(local,upper,between(e-s,elbow-s)*g[upper][1]);g=fk(local)
 setrot(local,lower,between(g[end][0]-g[lower][0],target-g[lower][0])*g[lower][1])
def mixrot(a,b,t):return (a*R.from_rotvec((a.inv()*b).as_rotvec()*t))
def blend(a,b,t):
 return {n:[((1-t)*np.array(a[n][0])+t*np.array(b[n][0])).tolist(),mixrot(R.from_quat(a[n][1]),R.from_quat(b[n][1]),t).as_quat().tolist(),a[n][2]] for n in names}
M=np.diag([1,-1,1]);mapping={'pelvis':'hip','clavicle_r':'rCollar','upperarm_r':'rShldr','lowerarm_r':'rForeArm','hand_r':'rHand','clavicle_l':'lCollar','upperarm_l':'lShldr','lowerarm_l':'lForeArm','hand_l':'lHand','thigh_r':'rThigh','calf_r':'rShin','foot_r':'rFoot','thigh_l':'lThigh','calf_l':'lShin','foot_l':'lFoot','head':'head'}
def retarget(m,t):
 src=m.sample(t);bind=m.sample(0)
 # Reflection converts the FBX right-handed space to this mesh's +Y-forward coordinates.
 delta={n:R.from_matrix(M@(src[n][1]*bind[n][1].inv()).as_matrix()@M) for n in m.names}
 yaw=delta['hip'].as_euler('xyz')[2];unturn=R.from_euler('z',-yaw)
 delta={n:unturn*q for n,q in delta.items()}
 goals={n:delta[s]*refq[n] for n,s in mapping.items()}
 for i in range(1,6):goals[f'spine_0{i}']=mixrot(delta['abdomen'],delta['chest'],(i-1)/4)*refq[f'spine_0{i}']
 for i in [1,2]:goals[f'neck_0{i}']=mixrot(delta['neck'],delta['head'],(i-1)*.5)*refq[f'neck_0{i}']
 out=copy.deepcopy(localref)
 for n in names:
  if n in goals:setrot(out,n,goals[n])
 # Preserve vertical weight shifts, with horizontal displacement removed for CharacterMovement.
 g=fk(out);desired=refp['pelvis'].copy();desired[2]=refp['pelvis'][2]+(src['hip'][0][2]-bind['hip'][0][2])*1.12
 out['pelvis'][0]=(refq['root'].inv().apply(desired-refp['root'])/refs['root'][2]).tolist()
 # Ground the lowest foot; the source lacks toe markers, so preserve the target foot roll.
 g=fk(out);bottom=min(g['foot_l'][0][2]-refp['foot_l'][2],g['foot_r'][0][2]-refp['foot_r'][2])
 desired[2]-=bottom
 out['pelvis'][0]=(refq['root'].inv().apply(desired-refp['root'])/refs['root'][2]).tolist()
 return out
def curl(local,amount):
 for side in ['l','r']:
  # Flex around the palm's lateral axis, toward the palm rather than twisting the fingers.
  lateral=unit(refp['pinky_01_'+side]-refp['index_01_'+side])
  direction=unit(refp['middle_03_'+side]-refp['hand_'+side])
  if np.cross(lateral,direction)[2]>0:lateral=-lateral
  for finger in ['index','middle','ring','pinky']:
   for j,angle in [(1,amount*.5),(2,amount),(3,amount*.65)]:
    n=f'{finger}_0{j}_{side}';axis=refq[n].inv().apply(lateral)
    local[n][1]=(R.from_quat(local[n][1])*R.from_rotvec(axis*np.deg2rad(angle))).as_quat().tolist()
def close_loop(poses,window=.2):
 count=len(poses)-1
 # Crossfade over the seam on both sides, avoiding a velocity discontinuity at wrap.
 width=round(count*window)
 first,last=copy.deepcopy(poses[0]),copy.deepcopy(poses[-1]);mid=blend(first,last,.5)
 for i in range(width+1):
  x=i/width;w=(1-x)**2*(1+2*x)
  poses[i]=blend(poses[i],mid,w*.5 if i else 1)
  poses[-1-i]=blend(poses[-1-i],mid,w*.5 if i else 1)
 # Correct the endpoints with a continuous smooth distribution instead of the above endpoint jump.
 for i in range(width+1):
  w=(1-i/width)**2*(1+2*i/width)
  poses[i]=blend(poses[i],mid,w)
  poses[-1-i]=blend(poses[-1-i],mid,w)
 poses[-1]=copy.deepcopy(poses[0])
 return poses
dribble=Motion(ROOT/'06_03.fbx');shot=Motion(ROOT/'06_15.fbx');stance=retarget(Motion(ROOT/'06_13.fbx'),7.45)
stanceg=fk(stance);anchors={'l':np.array([24.,-7.,refp['foot_l'][2]]),'r':np.array([-24.,8.,refp['foot_r'][2]])}
def palm_pose(local,normal,direction):
 normal=unit(normal);direction=unit(direction-normal*np.dot(direction,normal))
 rd=unit(refp['middle_03_r']-refp['hand_r']);rn=unit(np.cross(refp['index_01_r']-refp['pinky_01_r'],rd))
 if rn[2]>0:rn=-rn
 rd=unit(rd-rn*np.dot(rd,rn));a=np.column_stack([rd,np.cross(rn,rd),rn]);b=np.column_stack([direction,np.cross(normal,direction),normal])
 setrot(local,'hand_r',R.from_matrix(b@a.T)*refq['hand_r'])
out={};nframes=54
for kind in ['DribbleIdle','DribbleRun']:
 poses=[]
 for i in range(nframes+1):
  phase=i/nframes;pose=retarget(dribble,2.933333+phase*.9)
  if kind=='DribbleRun':
   base=T['clips']['Run'][round(phase*(len(T['clips']['Run'])-1))]
   for n in names:
    if n=='pelvis' or any(n.startswith(p) for p in ['thigh','calf','foot','ball','ik_foot']):pose[n]=copy.deepcopy(base[n])
  else:
   # A planted athletic stance with captured torso motion, not walking in place.
   pg=fk(pose);height=refp['pelvis'][2]-7+1.0*math.sin(phase*2*math.pi)
   p=refp['pelvis'].copy();p[2]=height
   pose['pelvis'][0]=(refq['root'].inv().apply(p-refp['root'])/refs['root'][2]).tolist()
   pose['pelvis'][1]=copy.deepcopy(stance['pelvis'][1])
   for side in ['l','r']:
    for prefix in ['thigh_','calf_','foot_','ball_']:pose[prefix+side]=copy.deepcopy(stance[prefix+side])
    solve(pose,'thigh_'+side,'calf_'+side,'foot_'+side,anchors[side],anchors[side]+np.array([0,90,50]))
    setrot(pose,'foot_'+side,refq['foot_'+side])
  palm_pose(pose,np.array([0.,.15,-1.]),np.array([-.12,1.,.12]))
  curl(pose,20);poses.append(pose)
 out[kind]={'fps':60,'poses':close_loop(poses)}
# Natural gather, extension and follow-through from CMU shooting capture.
poses=[]
for i in range(91):
 t=1.3+i/90*2.15;pose=retarget(shot,t)
 # Grounded shot: use a stable base while preserving the capture's torso and arm motion.
 height=refp['pelvis'][2]-7-5*math.sin(math.pi*min(1,i/63))
 p=refp['pelvis'].copy();p[2]=height;pose['pelvis'][0]=(refq['root'].inv().apply(p-refp['root'])/refs['root'][2]).tolist()
 for side in ['l','r']:
  for prefix in ['thigh_','calf_','foot_','ball_']:pose[prefix+side]=copy.deepcopy(stance[prefix+side])
  solve(pose,'thigh_'+side,'calf_'+side,'foot_'+side,anchors[side],anchors[side]+np.array([0,90,50]));setrot(pose,'foot_'+side,refq['foot_'+side])
 curl(pose,25 if t<2.8 else 12);poses.append(pose)
out['Throw']={'fps':60,'poses':poses}
for kind,clip in out.items():
 clip['names']=names
(ROOT/'retargeted.json').write_text(json.dumps(out))
# Thumb/index landmarks define the actual palm attachment offset.
direction=unit(refp['middle_03_r']-refp['hand_r']);palm=unit(np.cross(refp['index_01_r']-refp['pinky_01_r'],direction))
if palm[2]>0:palm=-palm
offset=refq['hand_r'].inv().apply(direction*6+palm*11)
(ROOT/'socket.json').write_text(json.dumps({'offset':offset.tolist(),'release_time':(2.8-1.3)/2.15*1.5}))
print('Saved clips',[(k,len(v['poses'])) for k,v in out.items()],'socket',offset)
# Retargeted stick sheets for checking coordinates before writing assets.
class Target:
 parents=parents
 def __init__(self,poses):self.poses=poses
 def sample(self,t):
  p=fk(self.poses[min(len(self.poses)-1,round(t*60))]);p['hip']=p['pelvis'];return p
for kind,clip in out.items():sheet(Target(clip['poses']),ROOT/(kind+'-target.png'),np.linspace(0,(len(clip['poses'])-1)/60,12))
