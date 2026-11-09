"""Rebuild pack-only basketball motion, palm contact curves, gaze correction and Pose Search.
Run in the UE editor Python console with L_ArtTest open and PIE stopped.
Inputs are the already retargeted KB_ clips and BasketballMotion.json beside this script.
RetargetAnimations.py regenerates those clips from the imported RootMotion pack if needed.
"""
import unreal,json
from pathlib import Path
out={}
data=json.loads((Path(__file__).parent/'BasketballMotion.json').read_text(encoding='utf-8'));root='/Game/Lockdown/Art_Test/Characters/Kobe/Basketball';tools=unreal.AssetToolsHelpers.get_asset_tools()
def asset(name,cls,factory):
 p=root+'/'+name
 return unreal.load_asset(p) if unreal.EditorAssetLibrary.does_asset_exist(p) else tools.create_asset(name,root,cls,factory)
clips={x['id']:x for x in data['clips']};anims={}
assert all(n<178 and 'source' not in r for n,r in clips.items())
for n,row in clips.items():
 a=unreal.load_asset(row['path']);anims[n]=a
 if a is None: raise RuntimeError('Missing retargeted clip: '+row['path'])
 a.set_editor_property('force_root_lock',True)
 controller=a.controller
 controller.open_bracket('Basketball motion polish',False)
 for bone,track in row['tracks'].items():
  controller.set_bone_track_keys(bone,[unreal.Vector(*v) for v in track['p']],[unreal.Quat(*v) for v in track['q']],[unreal.Vector(*v) for v in track['s']],False)
 controller.close_bracket(False)
 for name,vals in [('BallX',[p[0] for p in row['ball']]),('BallY',[p[1] for p in row['ball']]),('BallZ',[p[2] for p in row['ball']]),('BallLeft',row['left']),('BallContact',row['contact'])]:
  if unreal.AnimationLibrary.does_curve_exist(a,name,unreal.RawCurveTrackTypes.RCT_FLOAT):unreal.AnimationLibrary.remove_curve(a,name,False)
  unreal.AnimationLibrary.add_curve(a,name,unreal.RawCurveTrackTypes.RCT_FLOAT,False)
  unreal.AnimationLibrary.add_float_curve_keys(a,name,row['times'],vals)
 if 'turn_facing' in row:
  if unreal.AnimationLibrary.does_curve_exist(a,'TurnFacing',unreal.RawCurveTrackTypes.RCT_FLOAT):unreal.AnimationLibrary.remove_curve(a,'TurnFacing',False)
  unreal.AnimationLibrary.add_curve(a,'TurnFacing',unreal.RawCurveTrackTypes.RCT_FLOAT,False)
  unreal.AnimationLibrary.add_float_curve_keys(a,'TurnFacing',row['times'],row['turn_facing'])
 unreal.EditorAssetLibrary.save_loaded_asset(a)
out['curves']=len(anims)
mesh=unreal.load_asset('/Game/Lockdown/Art_Test/Characters/Kobe/SK_BasketballHero');skel=mesh.skeleton
sf=unreal.DataAssetFactory();sf.set_editor_property('data_asset_class',unreal.PoseSearchSchema)
schema=asset('PSS_KobeBasketball',unreal.PoseSearchSchema,sf)
role=unreal.PoseSearchRoledSkeleton();role.set_editor_property('skeleton',skel);role.set_editor_property('role','None')
schema.set_editor_property('skeletons',[role]);schema.set_editor_property('sample_rate',30)
trajectory=unreal.new_object(unreal.PoseSearchFeatureChannel_Trajectory,outer=schema)
samples=[]
for t,w in [(-.2,.5),(0,1),(.2,1.5),(.4,2),(.7,1)]:
 x=unreal.PoseSearchTrajectorySample();x.set_editor_property('offset',t);x.set_editor_property('flags',16|32|128);x.set_editor_property('weight',w);samples.append(x)
trajectory.set_editor_property('samples',samples);trajectory.set_editor_property('weight',4.)
pose=unreal.new_object(unreal.PoseSearchFeatureChannel_Pose,outer=schema);bones=[]
for n,w in [('pelvis',.7),('foot_l',1.5),('foot_r',1.5),('hand_l',.5),('hand_r',.5)]:
 b=unreal.PoseSearchBone();r=unreal.BoneReference();r.set_editor_property('bone_name',n);b.set_editor_property('reference',r);b.set_editor_property('flags',3);b.set_editor_property('weight',w);bones.append(b)
pose.set_editor_property('sampled_bones',bones);pose.set_editor_property('weight',1.)
schema.set_editor_property('channels',[trajectory,pose]);unreal.EditorAssetLibrary.save_loaded_asset(schema)
def database(name,ids):
 df=unreal.DataAssetFactory();df.set_editor_property('data_asset_class',unreal.PoseSearchDatabase)
 db=asset(name,unreal.PoseSearchDatabase,df);db.set_editor_property('schema',schema);db.set_editor_property('preview_mesh',mesh)
 assert unreal.LDBasketballMotionSet.set_database_animations(db,[anims[n] for n in ids])
 db.set_editor_property('pose_search_mode',unreal.PoseSearchMode.BRUTE_FORCE)
 db.set_editor_property('continuing_pose_cost_bias',-.5)
 db.set_editor_property('exclude_from_database_parameters',unreal.FloatInterval(0,-.12))
 unreal.EditorAssetLibrary.save_loaded_asset(db);out[name]={'assets':db.get_num_animation_assets(),'poses':db.get_num_poses() if hasattr(db,'get_num_poses') else 'index built on save'}
 return db
dribble=database('PSD_Kobe_Dribble',[7])
idle=database('PSD_Kobe_Idle',[2,11,12])
handling=database('PSD_Kobe_Handling',[43,44])
carry=database('PSD_Kobe_Carry',[7])
factory=unreal.DataAssetFactory();factory.set_editor_property('data_asset_class',unreal.LDBasketballMotionSet)
motion=asset('DA_KobeBasketball',unreal.LDBasketballMotionSet,factory)
for n,v in [('dribble',dribble),('idle',idle),('handling',handling),('carry',carry)]:motion.set_editor_property(n,v)
groups={'passes':[78,79],'standing_shots':[148,149,152,153,166,167],'moving_shots':[156,157,158,159,162,163],'crossovers':[41],'catches':list(range(80,87)),'stops':[],'turns':[98,99]}
for key,ids in groups.items():
 arr=[]
 for n in ids:
  r=clips[n];c=unreal.LDBasketballActionClip();c.set_editor_properties({'animation':anims[n],'start':r['start'],'release':r['release'],'end':r['end'],'left_hand':r['left_hand']});arr.append(c)
 motion.set_editor_property(key,arr)
unreal.EditorAssetLibrary.save_loaded_asset(motion)
# Only update the existing art-test character's motion reference and cadence.
actor_sub=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
hero=next((a for a in actor_sub.get_all_level_actors() if a.get_class().get_name()=='LDArtTestCharacter'),None)
if hero is None: raise RuntimeError('Open L_ArtTest before authoring basketball assets.')
hero.set_editor_property('motion_set',motion);hero.set_editor_property('preview_attacks_per_second',2.);hero.set_editor_property('animation_run_speed',420.)
for name,location in data['sockets'].items():
 assert unreal.LDBasketballMotionSet.set_palm_socket(mesh,'BasketballSocketL' if name=='l' else 'BasketballSocket',unreal.Vector(*location))
unreal.EditorAssetLibrary.save_loaded_asset(mesh)
unreal.EditorAssetLibrary.save_loaded_asset(skel)
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
out['saved']=True
report=Path(unreal.Paths.project_dir())/'Artifacts'/'BasketballPackageAuthoring.json'
report.parent.mkdir(parents=True,exist_ok=True)
report.write_text(json.dumps(out,indent=2),encoding='utf-8')
unreal.log('Basketball motion assets rebuilt: '+str(report))

