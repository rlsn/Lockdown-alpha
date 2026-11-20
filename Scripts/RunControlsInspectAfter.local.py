import json
from pathlib import Path
import unreal
out=Path('D:/MonkeyAI/output/run-controls-20261115/post-restart')
out.mkdir(parents=True,exist_ok=True)
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
rows=[]
for a in actors:
    p=a.get_actor_location(); t=a.get_actor_transform(); b=a.get_actor_bounds(False)
    row={'name':a.get_name(),'label':a.get_actor_label(),'class':a.get_class().get_name(),'tags':[str(v) for v in a.tags],
         'position':[p.x,p.y,p.z],'transform':str(t),'bounds':[[b[0].x,b[0].y,b[0].z],[b[1].x,b[1].y,b[1].z]],
         'parent':a.get_attach_parent_actor().get_name() if a.get_attach_parent_actor() else None}
    if isinstance(a,unreal.StaticMeshActor):
        c=a.static_mesh_component
        row['mesh']=c.static_mesh.get_path_name() if c.static_mesh else None
        row['collision']=str(c.get_collision_enabled())
    rows.append(row)
report={'project':unreal.Paths.get_project_file_path(),'world':world.get_path_name(),
        'pie':unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor(),
        'dirty_maps':[p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()],
        'validation':unreal.LDSceneAuthoringLibrary.ground_validation_error(world),'actors':rows}
(out/'current-scene.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({k:v for k,v in report.items() if k!='actors'},ensure_ascii=False))
print('actors',len(rows),'snapshot',str(out/'current-scene.json'))


