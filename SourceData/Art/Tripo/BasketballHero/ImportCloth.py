import unreal,json,traceback
from pathlib import Path
root=Path(unreal.Paths.project_dir())/'Artifacts'/'BasketballClothPolish';root.mkdir(parents=True,exist_ok=True)
try:
 folder='/Game/Lockdown/Art_Test/Characters/Kobe';path=folder+'/SK_BasketballHero';mesh=unreal.load_asset(path)
 skeleton=mesh.skeleton;materials=list(mesh.materials);physics=mesh.physics_asset
 component=unreal.new_object(unreal.SkeletalMeshComponent);component.set_skeletal_mesh_asset(mesh)
 socket_before=component.get_socket_transform('BasketballSocket',unreal.RelativeTransformSpace.RTS_PARENT_BONE_SPACE)
 task=unreal.AssetImportTask();task.filename=str(Path(unreal.Paths.project_dir())/'SourceData/Art/Tripo/BasketballHero/BasketballHero_ClothPolish.fbx');task.destination_path=folder;task.destination_name='SK_BasketballHero'
 task.automated=True;task.replace_existing=True;task.replace_existing_settings=True;task.save=True;task.factory=unreal.FbxFactory()
 opt=unreal.FbxImportUI();opt.automated_import_should_detect_type=False;opt.mesh_type_to_import=unreal.FBXImportType.FBXIT_SKELETAL_MESH
 opt.import_as_skeletal=True;opt.import_mesh=True;opt.import_animations=False;opt.import_materials=False;opt.import_textures=False;opt.create_physics_asset=False;opt.skeleton=skeleton
 sd=opt.skeletal_mesh_import_data;sd.import_uniform_scale=182.31454467773438;sd.normal_import_method=unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS;sd.set_editor_property('update_skeleton_reference_pose',False)
 task.options=opt
 world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world();previous=unreal.SystemLibrary.get_console_variable_int_value('Interchange.FeatureFlags.Import.FBX')
 unreal.SystemLibrary.execute_console_command(world,'Interchange.FeatureFlags.Import.FBX 0')
 try:unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
 finally:unreal.SystemLibrary.execute_console_command(world,'Interchange.FeatureFlags.Import.FBX '+str(previous))
 mesh=unreal.load_asset(path);mesh.set_editor_property('materials',materials);mesh.set_editor_property('physics_asset',physics)
 component.set_skeletal_mesh_asset(mesh);socket_after=component.get_socket_transform('BasketballSocket',unreal.RelativeTransformSpace.RTS_PARENT_BONE_SPACE)
 report={'imported':list(task.imported_object_paths),'skeleton_matches':mesh.skeleton==skeleton,'bones':component.get_num_bones(),'height':mesh.get_bounds().box_extent.z*2,'socket':component.does_socket_exist('BasketballSocket'),'socket_offset_error':(socket_after.translation-socket_before.translation).length(),'physics_matches':mesh.physics_asset==physics,'animations':{}}
 for kind in ['Idle','Run','DribbleIdle','DribbleRun','Throw']:
  a=unreal.load_asset(folder+'/Animations/A_BasketballHero_'+kind);report['animations'][kind]=a.get_editor_property('skeleton')==skeleton
 assert report['skeleton_matches'] and report['bones']==80 and report['socket'] and report['socket_offset_error']<1e-6 and all(report['animations'].values())
 unreal.EditorAssetLibrary.save_loaded_asset(mesh)
 report['dirty_maps']=[p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()]
 report['dirty_content']=[p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
 (root/'landing_report.json').write_text(json.dumps(report,indent=2));unreal.log('LD_GARMENT_REPAIR_LANDED')
except Exception:
 (root/'landing_report.json').write_text(json.dumps({'error':traceback.format_exc()},indent=2));raise



