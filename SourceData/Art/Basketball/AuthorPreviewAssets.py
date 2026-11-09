import unreal,math,json,os

ROOT='/Game/Lockdown/Art_Test'
HERO=ROOT+'/Characters/Kobe'
ASSETS=ROOT+'/Basketball'
tools=unreal.AssetToolsHelpers.get_asset_tools()
lib=unreal.EditorAssetLibrary

def asset(name,cls,factory):
    path=ASSETS+'/'+name
    return unreal.load_asset(path) if lib.does_asset_exist(path) else tools.create_asset(name,ASSETS,cls,factory)

def material(name,color,roughness=.8):
    m=asset(name,unreal.Material,unreal.MaterialFactoryNew())
    unreal.MaterialEditingLibrary.delete_all_material_expressions(m)
    c=unreal.MaterialEditingLibrary.create_material_expression(m,unreal.MaterialExpressionConstant3Vector)
    c.set_editor_property('constant',unreal.LinearColor(*color,1))
    unreal.MaterialEditingLibrary.connect_material_property(c,'',unreal.MaterialProperty.MP_BASE_COLOR)
    r=unreal.MaterialEditingLibrary.create_material_expression(m,unreal.MaterialExpressionConstant)
    r.set_editor_property('r',roughness)
    unreal.MaterialEditingLibrary.connect_material_property(r,'',unreal.MaterialProperty.MP_ROUGHNESS)
    unreal.MaterialEditingLibrary.recompile_material(m)
    lib.save_loaded_asset(m)
    return m

ball=unreal.load_asset(ASSETS+'/SM_Basketball') if lib.does_asset_exist(ASSETS+'/SM_Basketball') else lib.duplicate_asset('/Engine/BasicShapes/Sphere',ASSETS+'/SM_Basketball')
m=asset('M_Basketball',unreal.Material,unreal.MaterialFactoryNew())
unreal.MaterialEditingLibrary.delete_all_material_expressions(m)
uv=unreal.MaterialEditingLibrary.create_material_expression(m,unreal.MaterialExpressionTextureCoordinate)
c=unreal.MaterialEditingLibrary.create_material_expression(m,unreal.MaterialExpressionCustom)
c.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT3)
ci=unreal.CustomInput();ci.set_editor_property('input_name','UV');c.set_editor_property('inputs',[ci])
code='''
float a=UV.x*6.2831853, b=UV.y*3.14159265;
float3 p=float3(sin(b)*cos(a),sin(b)*sin(a),cos(b));
float d=min(abs(p.z),abs(p.x));
float curved=abs(abs(p.y)-0.72*(1.0-0.28*p.z*p.z));
d=min(d,curved);
float seam=1.0-smoothstep(0.012,0.024,d);
float2 cell=float2(UV.x*300*sin(b),UV.y*150);
float pebble=smoothstep(0.42,0.15,length(frac(cell)-0.5));
float grain=frac(sin(dot(floor(cell),float2(127.1,311.7)))*43758.5453);
float3 leather=float3(0.48,0.115,0.018)*(0.82+0.17*pebble+0.08*grain);
return lerp(leather,float3(0.009,0.007,0.005),seam);
'''
c.set_editor_property('code',code)
unreal.MaterialEditingLibrary.connect_material_expressions(uv,'',c,'UV')
unreal.MaterialEditingLibrary.connect_material_property(c,'',unreal.MaterialProperty.MP_BASE_COLOR)
r=unreal.MaterialEditingLibrary.create_material_expression(m,unreal.MaterialExpressionConstant);r.set_editor_property('r',.87)
unreal.MaterialEditingLibrary.connect_material_property(r,'',unreal.MaterialProperty.MP_ROUGHNESS)
unreal.MaterialEditingLibrary.recompile_material(m)
ball.set_material(0,m)
for a in [m,ball]:lib.save_loaded_asset(a)
material('M_GolemStone',(.07,.17,.19))
material('M_GolemDark',(.018,.042,.05))
material('M_GolemEye',(.95,.15,.015),.35)


# Authored CMU motion tracks on the imported character skeleton.
import unreal,json
F='/Game/Lockdown/Art_Test/Characters/Kobe/Animations/A_BasketballHero_'
from pathlib import Path
data=json.loads((Path(__file__).parent/'BasketballMotion.json').read_text())
for kind,clip in data.items():
 if kind=='Ready':continue
 path=F+kind
 a=unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else unreal.EditorAssetLibrary.duplicate_asset(F+'Idle',path)
 c=a.controller;c.open_bracket('Retarget CMU basketball capture',False)
 try:
  c.set_frame_rate(unreal.FrameRate(clip['fps'],1),False);c.set_number_of_frames(unreal.FrameNumber(len(clip['poses'])-1),False)
  for n in clip['names']:
   poses=[p[n] for p in clip['poses']]
   if not a.data_model_interface.is_valid_bone_track_name(n):c.add_bone_track(n,False)
   assert c.set_bone_track_keys(n,[unreal.Vector(*p[0]) for p in poses],[unreal.Quat(*p[1]) for p in poses],[unreal.Vector(*p[2]) for p in poses],False)
 finally:c.close_bracket(False)
 unreal.EditorAssetLibrary.save_loaded_asset(a)
unreal.log('LD_BASKETBALL_MOCAP_IMPORTED')
