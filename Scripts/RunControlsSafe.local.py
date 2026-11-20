import unreal,json,shutil,math,collections
from pathlib import Path
out=Path('D:/MonkeyAI/output/run-controls-20261115')
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert world and world.get_name()=='L_Demo' and not level.is_in_play_in_editor()
sub=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
actors=list(sub.get_all_level_actors())
assert not (out/'paused-camps.json').exists()
mapfile=Path('D:/Lockdown-alpha/Content/Lockdown/Generated/Maps/L_Demo.umap')
shutil.copy2(mapfile,out/'before-disk.umap')
assert level.save_current_level()
shutil.copy2(mapfile,out/'before-current-edits.umap')
base=json.loads(Path('D:/Lockdown-alpha/SourceData/World/demo_world.json').read_text(encoding='utf-8'))
templates={a['id']:a for a in base['anchors']}
def val(a,p):return next((str(t)[len(p):] for t in a.tags if str(t).startswith(p)),None)
def pause(a):
    a.modify();a.tags=['LDRunMapPaused:'+str(t) if str(t).startswith(('LDSceneAnchor:','LDSceneMember:','LDScenePrototypeAnchor:')) else t for t in a.tags]
ignore=[]
for a in actors:
    parts=a.get_components_by_class(unreal.PrimitiveComponent)
    if parts and all('BLOCK' not in str(p.get_collision_response_to_channel(unreal.CollisionChannel.ECC_PAWN)) for p in parts):ignore.append(a)
objects=[unreal.ObjectTypeQuery.OBJECT_TYPE_QUERY1]
reserved=[]
def clear(p):
    hit=unreal.SystemLibrary.line_trace_single_for_objects(world,unreal.Vector(p.x,p.y,20000),unreal.Vector(p.x,p.y,-10000),objects,False,ignore,unreal.DrawDebugTrace.NONE)
    if not hit:return False
    data=hit.to_dict();foot=data['impact_point']
    if data['impact_normal'].z<.70:return False
    cap=unreal.SystemLibrary.capsule_trace_single_for_objects(world,foot+unreal.Vector(0,0,130),foot+unreal.Vector(0,0,87),40,90,objects,False,ignore,unreal.DrawDebugTrace.NONE)
    if not cap or cap.to_dict()['initial_overlap'] or cap.to_dict()['impact_normal'].z<.70:return False
    if unreal.SystemLibrary.capsule_overlap_actors(world,cap.to_dict()['location']+unreal.Vector(0,0,2.15),40,90,objects,unreal.Actor,ignore):return False
    if any(math.hypot(p.x-q.x,p.y-q.y)<105 for q in reserved):return False
    return True
paused=[];kept=[];centres=[]
for a in actors:
    ident=val(a,'LDSceneAnchor:');proto=val(a,'LDScenePrototypeAnchor:') or ident
    if proto in templates and templates[proto]['kind']=='Camp':centres.append((a,ident,proto))
seen=set()
for centre,ident,proto in centres:
    members=[a for a in actors if a.get_attach_parent_actor()==centre and val(a,'LDSceneMember:')]
    slots=[int(val(a,'LDSceneMemberSlot:') or val(a,'LDSceneMember:').rsplit('_',1)[1]) for a in members]
    reason=''
    if ident in seen:reason='duplicate camp identity'
    elif sorted(slots)!=list(range(len(templates[proto]['members']))):reason='copied or missing camp member slots'
    elif len(reserved)+len(members)>70:reason='temporary run-map unit limit'
    elif any(not clear(a.get_actor_location()) for a in members):reason='blocked or overlapping monster placement'
    if reason:
        paused.append({'camp':ident,'actor':centre.get_name(),'reason':reason,'members':[a.get_name() for a in members]})
        pause(centre)
        for a in members:pause(a)
    else:
        seen.add(ident);kept.append(ident);reserved.extend(a.get_actor_location() for a in members)
assert kept,'Need at least one valid existing camp'
# Unparented copied preview members have no runtime camp. Keep their editor appearance and placement.
for a in actors:
    if val(a,'LDSceneMember:') and not a.get_attach_parent_actor():pause(a)
disabled_points=[]
for attempt in range(80):
    error=unreal.LDSceneAuthoringLibrary.ground_validation_error(world)
    if not error:break
    import re
    m=re.search(r'(?:Layout point |layout point )([^ :]+)',error)
    a=next((a for a in actors if m and val(a,'LDSceneAnchor:')==m.group(1)),None)
    if a:
        ident=val(a,'LDSceneAnchor:');proto=val(a,'LDScenePrototypeAnchor:') or ident
        if proto in templates and templates[proto]['kind'] in ('Camp','Airdrop'):
            pause(a)
            for child in actors:
                if child.get_attach_parent_actor()==a and val(child,'LDSceneMember:'):pause(child)
            disabled_points.append({'id':ident,'reason':error});continue
    raise RuntimeError(error)
nav=unreal.LDSceneAuthoringLibrary.navigation_bake_error(world)
assert not nav,nav
assert level.save_current_level()
report={'kept_camps':kept,'active_monsters':sum(bool(val(a,'LDSceneMember:')) for a in sub.get_all_level_actors()),'paused_camps':paused,'paused_optional_points':disabled_points,'validation':error,'navigation':nav,'all_actor_positions_preserved':True}
(out/'paused-camps.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(report,ensure_ascii=False))

