import unreal,json,re,math
from pathlib import Path
out=Path('D:/MonkeyAI/output/run-controls-20261115/post-restart')
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
sub=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert world and not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
def val(a,p):return next((str(t)[len(p):] for t in a.tags if str(t).startswith(p)),None)
actors=list(sub.get_all_level_actors());moved=[]
for iteration in range(70):
    error=unreal.LDSceneAuthoringLibrary.ground_validation_error(world)
    if not error:break
    match=re.search(r'(?:Layout point |layout point |member home point |member )([^ :]+)',error)
    if not match:raise RuntimeError(error)
    ident=match.group(1)
    a=next((a for a in actors if val(a,'LDSceneAnchor:')==ident or val(a,'LDSceneMember:')==ident or (isinstance(a,unreal.PlayerStart) and str(a.player_start_tag)==ident)),None)
    assert a,'No marker for '+error
    old=a.get_actor_location();chosen=None
    for extent in (150,300,600,1200,2400):
        foot=old-unreal.Vector(0,0,90) if isinstance(a,unreal.PlayerStart) else old
        nav=unreal.NavigationSystemV1.project_point_to_navigation(world,foot,None,None,unreal.Vector(extent,extent,3000))
        if nav is None:continue
        p=nav+unreal.Vector(0,0,90) if isinstance(a,unreal.PlayerStart) else nav
        a.modify();a.set_actor_location(p,False,True)
        after=unreal.LDSceneAuthoringLibrary.ground_validation_error(world)
        if ident not in after:
            chosen=p;break
        a.set_actor_location(old,False,True)
    if chosen is None:raise RuntimeError('Cannot locally resolve '+error)
    moved.append({'id':ident,'actor':a.get_name(),'from':[old.x,old.y,old.z],'to':[chosen.x,chosen.y,chosen.z],'reason':error})
else:raise RuntimeError('Too many blocked markers')
report={'validation':error,'moved_gameplay_markers':moved,'monster_markers':sum(bool(val(a,'LDSceneMember:')) for a in actors)}
(out/'unblock-report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
assert not error,error
nav_error=unreal.LDSceneAuthoringLibrary.navigation_bake_error(world)
assert not nav_error,nav_error
assert unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
print(json.dumps(report,ensure_ascii=False))

