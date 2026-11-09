"""Regenerate only the 59 active basketball clips from the imported RootMotion pack.
Preserves source assets. Run AuthorAssets.py afterward to restore prop curves and search data.
The Tripo root bind scale must also be retained: correct root scale and pelvis translation only.
"""
import unreal,json
from pathlib import Path
out={}
root='/Game/Lockdown/Art_Test/Characters/Kobe/Basketball'
tools=unreal.AssetToolsHelpers.get_asset_tools()
src=unreal.load_asset('/Game/ArtTest/Basketball/Characters/Robot_A1/Meshes/Robot_A1')
dst=unreal.load_asset('/Game/Lockdown/Art_Test/Characters/Kobe/SK_BasketballHero')
def make(name,mesh,target):
 path=root+'/'+name
 rig=unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else tools.create_asset(name,root,unreal.IKRigDefinition,unreal.IKRigDefinitionFactory())
 c=unreal.IKRigController.get_controller(rig);c.set_skeletal_mesh(mesh);c.set_retarget_root('pelvis');c.set_root_motion_bone('root')
 chains=[('Spine','spine_01','spine_05' if target else 'spine_03'),('Head','neck_01','head')]
 for side,s in [('Left','l'),('Right','r')]:
  chains += [(side+'Clavicle','clavicle_'+s,'clavicle_'+s),(side+'Arm','upperarm_'+s,'hand_'+s),(side+'Leg','thigh_'+s,'foot_'+s),(side+'Toe','ball_'+s,'ball_'+s)]
  for f in ['thumb','index','middle','ring','pinky']:chains.append((side+f,f+'_01_'+s,f+'_03_'+s))
 existing={str(ch.chain_name) for ch in c.get_retarget_chains()}
 for n,a,b in chains:
  if n not in existing:c.add_retarget_chain(n,a,b,'')
 unreal.EditorAssetLibrary.save_loaded_asset(rig)
 return rig
a=make('IK_Basketball_Source',src,False);b=make('IK_Basketball_Kobe',dst,True)
name='RTG_Basketball_To_Kobe';path=root+'/'+name
ret=unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else tools.create_asset(name,root,unreal.IKRetargeter,unreal.IKRetargetFactory())
c=unreal.IKRetargeterController.get_controller(ret);c.remove_all_ops()
c.set_ik_rig(unreal.RetargetSourceOrTarget.SOURCE,a);c.set_ik_rig(unreal.RetargetSourceOrTarget.TARGET,b)
c.add_default_ops();c.auto_map_chains(unreal.AutoMapChainType.EXACT,True);c.auto_align_all_bones(unreal.RetargetSourceOrTarget.TARGET)
unreal.EditorAssetLibrary.save_loaded_asset(ret)
# Retain authored root trajectories for Pose Search; control the capsule separately at runtime.
manifest=json.loads((Path(__file__).parent/'BasketballMotion.json').read_text(encoding='utf-8'))
wanted={row['id'] for row in manifest['clips']}
reg=unreal.AssetRegistryHelpers.get_asset_registry()
assets=[x for x in reg.get_assets_by_path('/Game/ArtTest/Basketball/Animations/RootMotion',recursive=True) if str(x.asset_class_path.asset_name)=='AnimSequence' and int(str(x.asset_name)[:3]) in wanted]
ins=unreal.IKRetargetBatchOperationInputs();ins.assets_to_retarget=assets;ins.source_mesh=src;ins.target_mesh=dst;ins.ik_retarget_asset=ret
ins.target_path=root+'/Animations';ins.prefix='KB_';ins.overwrite_existing_files=True;ins.include_referenced_assets=False
results=unreal.IKRetargetBatchOperation.run_batch_retarget(ins)
out['results']=[]
for ad in results:
 seq=ad.get_asset();seq.set_editor_property('enable_root_motion',True);seq.set_editor_property('force_root_lock',False);seq.set_editor_property('loop','Loop' in seq.get_name())
 unreal.EditorAssetLibrary.save_loaded_asset(seq)
 out['results'].append({'path':seq.get_path_name(),'length':seq.get_play_length()})
out['count']=len(results)
out['ops']=[str(c.get_op_name(i)) for i in range(c.get_num_retarget_ops())]

rows=out['results']
rig=unreal.load_asset('/Game/Lockdown/Art_Test/Characters/Kobe/Basketball/IK_Basketball_Kobe')
scale=unreal.IKRigController.get_controller(rig).get_ref_pose_transform_of_bone('root').scale3d.x
opt=unreal.AnimPoseEvaluationOptions();opt.extract_root_motion=False;opt.incorporate_root_motion_into_pose=True;opt.should_retarget=False
out['repaired']=[]
for row in rows:
 a=unreal.load_asset(row['path']);c=a.controller;model=a.data_model_interface
 p0=unreal.AnimPoseExtensions.get_anim_pose_at_time(a,0,opt)
 raw=unreal.AnimPoseExtensions.get_bone_pose(p0,'root').scale3d.x
 if abs(raw-1)<.01:
  frames=model.get_number_of_frames();fps=model.get_frame_rate();hz=fps.numerator/fps.denominator
  poses=[unreal.AnimPoseExtensions.get_anim_pose_at_time(a,i/hz,opt) for i in range(frames+1)]
  c.open_bracket('Preserve Tripo bind scale in retarget root and pelvis',False)
  try:
   for n in ['root','pelvis']:
    ts=[unreal.AnimPoseExtensions.get_bone_pose(p,n) for p in poses]
    pos=[t.translation if n=='root' else unreal.Vector(t.translation.x/scale,t.translation.y/scale,t.translation.z/scale) for t in ts]
    scales=[unreal.Vector(scale,scale,scale) if n=='root' else t.scale3d for t in ts]
    c.set_bone_track_keys(n,pos,[t.rotation for t in ts],scales,False)
  finally:c.close_bracket(False)
 a.set_editor_property('force_root_lock',True)
 a.set_editor_property('root_motion_root_lock',unreal.RootMotionRootLock.ANIM_FIRST_FRAME)
 unreal.EditorAssetLibrary.save_loaded_asset(a);out['repaired'].append(a.get_name())
unreal.log('Retargeted and normalized '+str(len(rows))+' basketball clips; run AuthorAssets.py next.')
