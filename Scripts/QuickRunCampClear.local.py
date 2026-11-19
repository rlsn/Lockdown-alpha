import unreal,math,json
from pathlib import Path
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert world and not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
actors=list(unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors())
def val(a,p):return next((str(t)[len(p):] for t in a.tags if str(t).startswith(p)),None)
members=[a for a in actors if val(a,'LDSceneMember:')]
ignore=[]
for a in actors:
    parts=a.get_components_by_class(unreal.PrimitiveComponent)
    if parts and all('BLOCK' not in str(p.get_collision_response_to_channel(unreal.CollisionChannel.ECC_PAWN)) for p in parts):ignore.append(a)
objs=[unreal.ObjectTypeQuery.OBJECT_TYPE_QUERY1]
reserved=[];moved=[];failed=[]
def candidate(x,y):
    hit=unreal.SystemLibrary.line_trace_single_for_objects(world,unreal.Vector(x,y,20000),unreal.Vector(x,y,-10000),objs,False,ignore,unreal.DrawDebugTrace.NONE)
    if not hit or hit.to_dict()['impact_normal'].z<.70:return None
    p=hit.to_dict()['impact_point']
    nav=unreal.NavigationSystemV1.project_point_to_navigation(world,p,None,None,unreal.Vector(25,25,50))
    if nav is None or math.hypot(nav.x-x,nav.y-y)>30:return None
    if any(math.hypot(x-q.x,y-q.y)<105 for q in reserved):return None
    cap=unreal.SystemLibrary.capsule_trace_single_for_objects(world,p+unreal.Vector(0,0,130),p+unreal.Vector(0,0,87),40,90,objs,False,ignore,unreal.DrawDebugTrace.NONE)
    if not cap or cap.to_dict()['impact_normal'].z<.70:return None
    centre=cap.to_dict()['location']+unreal.Vector(0,0,2.15)
    overlap=unreal.SystemLibrary.capsule_overlap_actors(world,centre,40,90,objs,unreal.Actor,ignore)
    if overlap:return None
    return p
for a in members:
    old=a.get_actor_location();p=candidate(old.x,old.y)
    if p is None:
        for radius in (100,180,280,420,600,850,1200):
            for i in range(16):
                ang=i*math.pi/8;p=candidate(old.x+math.cos(ang)*radius,old.y+math.sin(ang)*radius)
                if p is not None:break
            if p is not None:break
    if p is None:failed.append(a.get_name());continue
    reserved.append(p)
    if math.dist([p.x,p.y,p.z],[old.x,old.y,old.z])>.1:
        a.modify();a.set_actor_location(p,False,True)
        moved.append({'id':val(a,'LDSceneMember:'),'actor':a.get_name(),'from':[old.x,old.y,old.z],'to':[p.x,p.y,p.z]})
report={'members':len(members),'adjusted':moved,'failed':failed,'ground_validation':unreal.LDSceneAuthoringLibrary.ground_validation_error(world)}
Path('D:/MonkeyAI/output/quick-run-repair/camp-clear.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(report,ensure_ascii=False))
assert not failed,failed
assert not report['ground_validation']
assert unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()





