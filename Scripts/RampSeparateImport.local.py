import unreal,json,math
from pathlib import Path
ROOT=Path('D:/MonkeyAI/output/ramp-separation-20261119');BASE='/Game/Lockdown/Art/IndependentRamps';lib=unreal.EditorAssetLibrary;at=unreal.AssetToolsHelpers.get_asset_tools();s=unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
assert unreal.Paths.get_project_file_path().replace('\\','/').lower()=='d:/lockdown-alpha/lockdown.uproject'
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world();assert world.get_path_name()=='/Game/Lockdown/Generated/Maps/L_Demo.L_Demo'
assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
manifest=json.loads((ROOT/'source/asset-manifest.json').read_text(encoding='utf8'));source={r['name']:r for r in json.loads((ROOT/'separated-meshes.json').read_text(encoding='utf8'))};flag='Interchange.FeatureFlags.Import.FBX';old=unreal.SystemLibrary.get_console_variable_bool_value(flag);report=[];dump={}
assert not any(c.static_mesh and c.static_mesh.get_path_name().startswith(BASE+'/')for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()for c in a.get_components_by_class(unreal.StaticMeshComponent)),'Task assets already placed; do not replace behind authored actors'
def area(a,b,c):
 u=[b[i]-a[i]for i in range(3)];v=[c[i]-a[i]for i in range(3)];return math.sqrt(sum(x*x for x in [u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]]))/2
try:
 unreal.SystemLibrary.execute_console_command(world,flag+' 0')
 for entry in manifest:
  dest=BASE+('/Ramps'if entry['kind']=='ramp'else'/GroundRemainders');path=dest+'/'+entry['name']
  ui=unreal.FbxImportUI();ui.import_mesh=True;ui.import_materials=False;ui.import_textures=False;ui.import_as_skeletal=False;ui.mesh_type_to_import=unreal.FBXImportType.FBXIT_STATIC_MESH;ui.automated_import_should_detect_type=False
  opt=ui.static_mesh_import_data;opt.combine_meshes=True;opt.remove_degenerates=False;opt.auto_generate_collision=False;opt.generate_lightmap_u_vs=False;opt.import_uniform_scale=100;opt.transform_vertex_to_absolute=True;opt.normal_import_method=unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS
  task=unreal.AssetImportTask();task.filename=entry['fbx'];task.destination_path=dest;task.destination_name=entry['name'];task.automated=True;task.replace_existing=True;task.save=False;task.options=ui;task.factory=unreal.FbxFactory();at.import_asset_tasks([task]);mesh=lib.load_asset(path);assert isinstance(mesh,unreal.StaticMesh),path
  mesh.set_editor_property('allow_cpu_access',True);paths={p.rsplit('/',1)[1].split('.')[0]:p for p in entry['materials']};assert len(paths)==len(entry['materials']);slots=[]
  for i,slot in enumerate(mesh.get_editor_property('static_materials')):
   name=str(slot.material_slot_name);assert name in paths,(path,name,list(paths));mesh.set_material(i,lib.load_asset(paths[name]));slots.append(paths[name])
  body=mesh.get_editor_property('body_setup');assert body;body.set_editor_property('collision_trace_flag',unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE);body.set_editor_property('double_sided_geometry',True)
  settings=s.get_lod_build_settings(mesh,0);settings.recompute_normals=False;settings.recompute_tangents=False;settings.remove_degenerates=False;s.set_lod_build_settings(mesh,0,settings)
  for section in range(mesh.get_num_sections(0)):s.enable_section_collision(mesh,True,0,section)
  bounds=mesh.get_bounding_box();actual=[[bounds.min.x,bounds.min.y,bounds.min.z],[bounds.max.x,bounds.max.y,bounds.max.z]];error=max(abs(actual[i][j]-entry['bounds_cm'][i][j])for i in range(2)for j in range(3));sections=[];triangles=0;actual_area=0
  for sec_i in range(mesh.get_num_sections(0)):
   data=unreal.ProceduralMeshLibrary.get_section_from_static_mesh(mesh,0,sec_i);vs=[[v.x,v.y,v.z]for v in data[0]];ids=list(data[1]);mi=s.get_lod_material_slot(mesh,0,sec_i);triangles+=len(ids)//3;actual_area+=sum(area(vs[ids[i]],vs[ids[i+1]],vs[ids[i+2]])for i in range(0,len(ids),3));sections.append({'vertices':vs,'indices':ids,'normals':[[n.x,n.y,n.z]for n in data[2]],'uvs':[[v.x,v.y]for v in data[3]],'material':slots[mi]})
  row=source[entry['name']];expected_area=sum(area(*[row['vertices'][j]for j in f])for f in row['faces']);assert error<.005 and abs(actual_area-expected_area)<max(.1,expected_area*.000004)and abs(triangles-entry['triangles'])<=5,(path,error,actual_area-expected_area,triangles,entry['triangles'])
  lib.save_loaded_asset(mesh);dump[entry['name']]={'asset':mesh.get_path_name(),'sections':sections};report.append({'asset':mesh.get_path_name(),'source_triangles':entry['triangles'],'imported_triangles':triangles,'bounds_error_cm':error,'surface_area_error_cm2':abs(actual_area-expected_area),'kind':entry['kind']});(ROOT/'import-report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
finally:unreal.SystemLibrary.execute_console_command(world,flag+(' 1'if old else' 0'))
(ROOT/'imported-geometry.json').write_text(json.dumps(dump),encoding='utf8');print('IMPORTED_VERIFIED',len(report),'ASSETS;',sum(r['imported_triangles']for r in report),'TRIANGLES; MAX_BOUNDS_ERROR_CM',max(r['bounds_error_cm']for r in report))
