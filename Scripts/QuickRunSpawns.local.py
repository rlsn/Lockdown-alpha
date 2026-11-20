import unreal,math,json
from pathlib import Path
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert w and not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
sub=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
starts=sorted([a for a in sub.get_all_level_actors() if isinstance(a,unreal.PlayerStart)],key=lambda a:str(a.player_start_tag))
objects=[unreal.ObjectTypeQuery.OBJECT_TYPE_QUERY1]
goal=unreal.Vector(0,0,500)
sites=[]
for radius in (800,1400,2000,2600):
    for i in range(16):
        x=math.cos(i*math.pi/8)*radius;y=math.sin(i*math.pi/8)*radius
        h=unreal.SystemLibrary.line_trace_single_for_objects(w,unreal.Vector(x,y,6000),unreal.Vector(x,y,-1000),objects,False,[],unreal.DrawDebugTrace.NONE)
        if not h:continue
        p=h.to_dict()['impact_point']
        if abs(p.z-500)>3:continue
        ok=True
        for dx,dy in ((0,0),(220,0),(-220,0),(0,220),(0,-220),(160,160),(160,-160),(-160,160),(-160,-160)):
            q=p+unreal.Vector(dx,dy,0)
            nav=unreal.NavigationSystemV1.project_point_to_navigation(w,q,None,None,unreal.Vector(20,20,30))
            if nav is None or math.hypot(nav.x-q.x,nav.y-q.y)>10:ok=False;break
            path=unreal.NavigationSystemV1.find_path_to_location_synchronously(w,nav,goal)
            if not path or not path.is_valid() or path.is_partial():ok=False;break
        if ok:sites.append(p)
    if len(sites)>=len(starts):break
assert len(sites)>=4,(len(sites),len(starts))
changes=[]
for a,p in zip(starts,(sites*4)):
    p=unreal.Vector(round(p.x,3),round(p.y,3),round(p.z,3))
    old=a.get_actor_location();a.modify();a.set_actor_location(p+unreal.Vector(0,0,90),False,True)
    changes.append({'id':str(a.player_start_tag),'from':[old.x,old.y,old.z],'to':[p.x,p.y,p.z+90]})
error=unreal.LDSceneAuthoringLibrary.ground_validation_error(w)
assert not error,error
assert unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
Path('D:/MonkeyAI/output/quick-run-repair/spawns.json').write_text(json.dumps(changes,indent=2),encoding='utf-8')
print('Safe central spawn sites saved:',len(changes))


