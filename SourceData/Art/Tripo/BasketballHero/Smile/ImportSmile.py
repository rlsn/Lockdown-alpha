import unreal,json,traceback
from pathlib import Path
p=Path(__file__).parent;report=Path(unreal.Paths.project_dir())/'Artifacts'/'BasketballSmile';report.mkdir(parents=True,exist_ok=True);folder='/Game/Lockdown/Art_Test/Characters/Kobe';tools=unreal.AssetToolsHelpers.get_asset_tools();lib=unreal.MaterialEditingLibrary
def asset(name,cls,factory,path=folder):
 return unreal.load_asset(path+'/'+name) if unreal.EditorAssetLibrary.does_asset_exist(path+'/'+name) else tools.create_asset(name,path,cls,factory)
def expr(mat,cls):return lib.create_material_expression(mat,cls)
def channel(mat,key):
 uv=expr(mat,unreal.MaterialExpressionTextureCoordinate);uv.set_editor_property('coordinate_index',1 if key in ['R','G'] else 2)
 mask=expr(mat,unreal.MaterialExpressionComponentMask);mask.set_editor_property('r',key in ['R','B']);mask.set_editor_property('g',key in ['G','A']);mask.set_editor_property('b',False);mask.set_editor_property('a',False);assert lib.connect_material_expressions(uv,'',mask,lib.get_material_expression_input_names(mask)[0]);return mask

try:
 files=list((p/'Head18').rglob('*'));textures={}
 for key,match in [('BaseColor','tripo_rgb_'),('Normal','tripo_normal_'),('Roughness','_roughness'),('Metallic','_metallic')]:
  file=next(f for f in files if f.is_file() and match in f.name);task=unreal.AssetImportTask();task.filename=str(file);task.destination_path=folder+'/Smile';task.destination_name='T_KobeSmile_'+key;task.automated=True;task.save=True;task.replace_existing=True;tools.import_asset_tasks([task]);tex=unreal.load_asset(folder+'/Smile/T_KobeSmile_'+key)
  if key!='BaseColor':tex.set_editor_property('srgb',False)
  if key=='Normal':tex.set_editor_property('compression_settings',unreal.TextureCompressionSettings.TC_NORMALMAP);tex.set_editor_property('flip_green_channel',True)
  unreal.EditorAssetLibrary.save_loaded_asset(tex);textures[key]=tex
 head=asset('M_KobeSmile',unreal.Material,unreal.MaterialFactoryNew(),folder+'/Smile');lib.delete_all_material_expressions(head);head.set_editor_property('two_sided',True);head.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
 for key,prop in [('BaseColor',unreal.MaterialProperty.MP_BASE_COLOR),('Normal',unreal.MaterialProperty.MP_NORMAL),('Roughness',unreal.MaterialProperty.MP_ROUGHNESS),('Metallic',unreal.MaterialProperty.MP_METALLIC)]:
  n=expr(head,unreal.MaterialExpressionTextureSample);n.texture=textures[key]
  if key=='Normal':n.sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL
  elif key!='BaseColor':n.sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR
  if key=='BaseColor':
   neck=expr(head,unreal.MaterialExpressionConstant3Vector);neck.constant=unreal.LinearColor(.15896084,.04231141,.01599629,0)
   color=expr(head,unreal.MaterialExpressionVertexColor);blend=expr(head,unreal.MaterialExpressionLinearInterpolate);lib.connect_material_expressions(n,'RGB',blend,'A');lib.connect_material_expressions(neck,'',blend,'B');lib.connect_material_expressions(channel(head,'B'),'',blend,'Alpha');lib.connect_material_property(blend,'',prop)
  elif key=='Normal':
   neutral=expr(head,unreal.MaterialExpressionConstant3Vector);neutral.constant=unreal.LinearColor(0,0,1,0);color=expr(head,unreal.MaterialExpressionVertexColor);blend=expr(head,unreal.MaterialExpressionLinearInterpolate);lib.connect_material_expressions(n,'RGB',blend,'A');lib.connect_material_expressions(neutral,'',blend,'B');lib.connect_material_expressions(channel(head,'B'),'',blend,'Alpha');lib.connect_material_property(blend,'',prop)
  else:
   neutral=expr(head,unreal.MaterialExpressionConstant);neutral.r=.65 if key=='Roughness' else 0
   color=expr(head,unreal.MaterialExpressionVertexColor);blend=expr(head,unreal.MaterialExpressionLinearInterpolate);lib.connect_material_expressions(n,'R',blend,'A');lib.connect_material_expressions(neutral,'',blend,'B');lib.connect_material_expressions(channel(head,'B'),'',blend,'Alpha');lib.connect_material_property(blend,'',prop)
 lib.recompile_material(head);unreal.EditorAssetLibrary.save_loaded_asset(head)
 bodypath=folder+'/M_KobeBodyCloth'
 if not unreal.EditorAssetLibrary.does_asset_exist(bodypath):unreal.EditorAssetLibrary.duplicate_asset(folder+'/tripo_mat_e5a46437',bodypath)
 body=unreal.load_asset(bodypath);body.set_editor_property('two_sided',True);body.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_DEFAULT_LIT);lib.delete_all_material_expressions(body)
 for key,prop in [('basecolor',unreal.MaterialProperty.MP_BASE_COLOR),('normal',unreal.MaterialProperty.MP_NORMAL),('roughness',unreal.MaterialProperty.MP_ROUGHNESS)]:
  n=expr(body,unreal.MaterialExpressionTextureSample);n.texture=unreal.load_asset(folder+'/SK_BasketballHero_UE5_'+key)
  n.sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL if key=='normal' else (unreal.MaterialSamplerType.SAMPLERTYPE_COLOR if key=='basecolor' else unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
  if key=='basecolor':
   neck=expr(body,unreal.MaterialExpressionConstant3Vector);neck.constant=unreal.LinearColor(.15896084,.04231141,.01599629,0)
   color=expr(body,unreal.MaterialExpressionVertexColor)
   tint=expr(body,unreal.MaterialExpressionConstant3Vector);tint.constant=unreal.LinearColor(.597,.362,.312,0)
   mul=expr(body,unreal.MaterialExpressionMultiply);lib.connect_material_expressions(n,'RGB',mul,'A');lib.connect_material_expressions(tint,'',mul,'B')
   skin=expr(body,unreal.MaterialExpressionLinearInterpolate);lib.connect_material_expressions(n,'RGB',skin,'A');lib.connect_material_expressions(mul,'',skin,'B');lib.connect_material_expressions(channel(body,'A'),'',skin,'Alpha')
   blend=expr(body,unreal.MaterialExpressionLinearInterpolate);lib.connect_material_expressions(skin,'',blend,'A');lib.connect_material_expressions(neck,'',blend,'B');lib.connect_material_expressions(channel(body,'B'),'',blend,'Alpha');assert lib.connect_material_property(blend,'',prop)
  elif key=='normal':
   neutral=expr(body,unreal.MaterialExpressionConstant3Vector);neutral.constant=unreal.LinearColor(0,0,1,0);color=expr(body,unreal.MaterialExpressionVertexColor);blend=expr(body,unreal.MaterialExpressionLinearInterpolate);lib.connect_material_expressions(n,'RGB',blend,'A');lib.connect_material_expressions(neutral,'',blend,'B');lib.connect_material_expressions(channel(body,'B'),'',blend,'Alpha');assert lib.connect_material_property(blend,'',prop)
  else:
   neutral=expr(body,unreal.MaterialExpressionConstant);neutral.r=.65;color=expr(body,unreal.MaterialExpressionVertexColor);blend=expr(body,unreal.MaterialExpressionLinearInterpolate);lib.connect_material_expressions(n,'R',blend,'A');lib.connect_material_expressions(neutral,'',blend,'B');lib.connect_material_expressions(channel(body,'B'),'',blend,'Alpha');assert lib.connect_material_property(blend,'',prop)
 vertex=expr(body,unreal.MaterialExpressionVertexColor);time=expr(body,unreal.MaterialExpressionTime);sin=expr(body,unreal.MaterialExpressionSine);sin.set_editor_property('period',.65)
 phase=expr(body,unreal.MaterialExpressionAdd);lib.connect_material_expressions(time,'',phase,'A');lib.connect_material_expressions(channel(body,'G'),'',phase,'B');assert lib.connect_material_expressions(phase,'',sin,lib.get_material_expression_input_names(sin)[0])
 direction=expr(body,unreal.MaterialExpressionConstant3Vector);direction.constant=unreal.LinearColor(0,1.0,.3,0)
 wave=expr(body,unreal.MaterialExpressionMultiply);lib.connect_material_expressions(sin,'',wave,'A');lib.connect_material_expressions(direction,'',wave,'B')
 strength=expr(body,unreal.MaterialExpressionScalarParameter);strength.set_editor_property('parameter_name','ClothFlutter');strength.set_editor_property('default_value',.15)
 flutter=expr(body,unreal.MaterialExpressionMultiply);lib.connect_material_expressions(wave,'',flutter,'A');lib.connect_material_expressions(strength,'',flutter,'B')
 inertia=expr(body,unreal.MaterialExpressionVectorParameter);inertia.set_editor_property('parameter_name','ClothInertia');inertia.set_editor_property('default_value',unreal.LinearColor(0,0,0,0))
 add=expr(body,unreal.MaterialExpressionAdd);lib.connect_material_expressions(flutter,'',add,'A');lib.connect_material_expressions(inertia,'',add,'B')
 mask=expr(body,unreal.MaterialExpressionMultiply);lib.connect_material_expressions(add,'',mask,'A');lib.connect_material_expressions(channel(body,'R'),'',mask,'B')
 transform=expr(body,unreal.MaterialExpressionTransform);transform.set_editor_property('transform_source_type',unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_LOCAL);transform.set_editor_property('transform_type',unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
 assert lib.connect_material_expressions(mask,'',transform,'');lib.connect_material_property(transform,'',unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET);lib.recompile_material(body);unreal.EditorAssetLibrary.save_loaded_asset(body)
 original=unreal.load_asset(folder+'/SK_BasketballHero');name='SK_BasketballHero';candidate=folder+'/'+name
 if not unreal.EditorAssetLibrary.does_asset_exist(candidate):unreal.EditorAssetLibrary.duplicate_asset(original.get_path_name(),candidate)
 task=unreal.AssetImportTask();task.filename=str(p/'BasketballHero_SmileCloth.fbx');task.destination_path=folder;task.destination_name=name;task.automated=True;task.replace_existing=True;task.replace_existing_settings=True;task.save=True;task.factory=unreal.FbxFactory()
 opt=unreal.FbxImportUI();opt.automated_import_should_detect_type=False;opt.mesh_type_to_import=unreal.FBXImportType.FBXIT_SKELETAL_MESH;opt.import_as_skeletal=True;opt.import_mesh=True;opt.import_animations=False;opt.import_materials=False;opt.import_textures=False;opt.create_physics_asset=False;opt.skeleton=original.skeleton
 sd=opt.skeletal_mesh_import_data;sd.import_uniform_scale=182.31454467773438;sd.normal_import_method=unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS;sd.set_editor_property('update_skeleton_reference_pose',False);sd.set_editor_property('vertex_color_import_option',unreal.VertexColorImportOption.REPLACE);task.options=opt
 world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world();previous=unreal.SystemLibrary.get_console_variable_int_value('Interchange.FeatureFlags.Import.FBX');unreal.SystemLibrary.execute_console_command(world,'Interchange.FeatureFlags.Import.FBX 0')
 try:tools.import_asset_tasks([task])
 finally:unreal.SystemLibrary.execute_console_command(world,'Interchange.FeatureFlags.Import.FBX '+str(previous))
 mesh=unreal.load_asset(candidate);mats=list(mesh.materials)
 assert len(mats)==2,len(mats)
 mats[0].material_interface=body;mats[1].material_interface=head;mesh.set_editor_property('materials',mats);mesh.set_editor_property('physics_asset',original.physics_asset)
 data=json.loads((Path(unreal.Paths.project_dir())/'SourceData/Art/Basketball/MotionMatching/BasketballMotion.json').read_text(encoding='utf-8'))
 for side,loc in data['sockets'].items():assert unreal.LDBasketballMotionSet.set_palm_socket(mesh,'BasketballSocketL' if side=='l' else 'BasketballSocket',unreal.Vector(*loc))
 component=unreal.new_object(unreal.SkeletalMeshComponent);component.set_skeletal_mesh_asset(mesh);assert component.get_num_bones()==80;assert abs(mesh.get_bounds().box_extent.z*2-180)<.1
 unreal.EditorAssetLibrary.save_loaded_asset(mesh);unreal.EditorAssetLibrary.save_loaded_asset(mesh.skeleton)
 (report/'landing.json').write_text(json.dumps({'imported':list(task.imported_object_paths),'materials':[m.material_interface.get_path_name() for m in mats],'height':mesh.get_bounds().box_extent.z*2,'skeleton':mesh.skeleton.get_path_name()},indent=2))
except Exception:
 (report/'landing.json').write_text(traceback.format_exc());raise




