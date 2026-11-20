import unreal,json,shutil
from pathlib import Path
out=Path('D:/MonkeyAI/output/slope50-no-ledge')
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert not level.is_in_play_in_editor()
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert world.get_path_name().startswith('/Game/Lockdown/Generated/Maps/L_Demo.'),world.get_path_name()
shutil.copy2('D:/Lockdown-alpha/Content/Lockdown/Generated/Maps/L_Demo.umap',out/'before-navigation-bake.umap')
error=unreal.LDSceneAuthoringLibrary.navigation_bake_error(world)
assert not error,error
movement=unreal.get_default_object(unreal.LDCharacter).get_component_by_class(unreal.CharacterMovementComponent)
navs=unreal.GameplayStatics.get_all_actors_of_class(world,unreal.RecastNavMesh)
links=[a.get_path_name() for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors() if 'LDDerivedLedgeLinks' in [str(t) for t in a.tags]]
result={'world':world.get_path_name(),'slope':movement.get_editor_property('walkable_floor_angle'),'walk_off_edges':movement.get_editor_property('can_walk_off_ledges'),
 'crouch_off_edges':movement.get_editor_property('can_walk_off_ledges_when_crouching'),'small_steps_cm':movement.get_editor_property('max_step_height'),
 'nav_slopes':[n.get_editor_property('agent_max_slope') for n in navs],'derived_drop_links':links}
assert result['slope']==50 and not result['walk_off_edges'] and not result['crouch_off_edges']
assert result['nav_slopes'] and all(a==50 for a in result['nav_slopes']) and not links
assert level.save_current_level()
(out/'live-settings.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(result,ensure_ascii=False))
