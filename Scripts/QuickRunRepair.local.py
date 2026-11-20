import unreal,json,shutil,math,collections
from pathlib import Path
out=Path('D:/MonkeyAI/output/quick-run-repair')
sub=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert world and world.get_name()=='L_Demo' and not level.is_in_play_in_editor()
assert not (out/'repair-applied.json').exists()
mapfile=Path('D:/Lockdown-alpha/Content/Lockdown/Generated/Maps/L_Demo.umap')
shutil.copy2(mapfile,out/'before-disk.umap')
assert level.save_current_level()
shutil.copy2(mapfile,out/'before-current-edits.umap')
actors=list(sub.get_all_level_actors())
templates={a['id']:a for a in json.loads(Path('D:/Lockdown-alpha/SourceData/World/demo_world.json').read_text(encoding='utf-8'))['anchors']}
def val(a,p):return next((str(t)[len(p):] for t in a.tags if str(t).startswith(p)),None)
def put(a,p,v):
    a.modify();a.tags=[t for t in a.tags if not str(t).startswith(p)]+[p+str(v)]
used={val(a,'LDSceneAnchor:') for a in actors}
def fresh(prefix):
    i=1
    while f'{prefix}{i:02}' in used:i+=1
    ident=f'{prefix}{i:02}';used.add(ident);return ident
changes=[];added=[]
def setup_centre(a,proto):
    ident=val(a,'LDSceneAnchor:')
    if ident.startswith('instance_'):
        ident=fresh(proto[0]);put(a,'LDSceneAnchor:',ident)
    a.set_actor_label(ident+'  营地中心')
    return ident
def set_member(a,ident,slot,proto):
    old=val(a,'LDSceneMember:');put(a,'LDSceneMember:',ident+'_'+str(slot));put(a,'LDSceneMemberSlot:',slot)
    a.set_actor_label(ident+'_'+str(slot)+'  '+('纪律委员' if proto.startswith('l') else '体育老师' if proto.startswith('h') else '催毕设的导师'))
    if old!=val(a,'LDSceneMember:'):changes.append({'actor':a.get_name(),'old':old,'new':val(a,'LDSceneMember:')})
for centre in actors:
    proto=val(centre,'LDScenePrototypeAnchor:')
    if not proto or templates[proto]['kind']!='Camp':continue
    ident=setup_centre(centre,proto)
    members=[m for m in actors if m.get_attach_parent_actor()==centre and val(m,'LDSceneMember:')]
    slots=collections.defaultdict(list)
    for m in members:slots[int(val(m,'LDSceneMemberSlot:') or '0')].append(m)
    for slot,group in slots.items():
        group.sort(key=lambda a:int(a.get_name().rsplit('_',1)[-1]))
        set_member(group[0],ident,slot,proto)
        for spare in group[1:]:
            newcentre=sub.spawn_actor_from_class(unreal.LDLayoutPoint,spare.get_actor_location(),unreal.Rotator())
            newcentre.set_folder_path('Gameplay/Camps/AuthorCopies')
            newid=fresh(proto[0]);put(newcentre,'LDSceneAnchor:',newid);put(newcentre,'LDScenePrototypeAnchor:',proto)
            newcentre.set_actor_label(newid+'  营地中心');added.append(newcentre.get_name())
            spare.attach_to_actor(newcentre,'',unreal.AttachmentRule.KEEP_WORLD,unreal.AttachmentRule.KEEP_WORLD,unreal.AttachmentRule.KEEP_WORLD,False)
            set_member(spare,newid,slot,proto)
            # Two-member low-camp templates require a complete pair. Keep the authored copy
            # and add its partner beside it, rather than dropping the copied monster.
            for missing in range(len(templates[proto]['members'])):
                if missing==slot:continue
                p=spare.get_actor_location()+unreal.Vector(0,160,0)
                partner=sub.spawn_actor_from_class(unreal.LDLayoutPoint,p,unreal.Rotator())
                partner.set_folder_path('Gameplay/Camps/AuthorCopies')
                partner.attach_to_actor(newcentre,'',unreal.AttachmentRule.KEEP_WORLD,unreal.AttachmentRule.KEEP_WORLD,unreal.AttachmentRule.KEEP_WORLD,False)
                set_member(partner,newid,missing,proto);added.append(partner.get_name())
validation=unreal.LDSceneAuthoringLibrary.ground_validation_error(world)
nav=unreal.LDSceneAuthoringLibrary.navigation_bake_error(world)
assert level.save_current_level()
allactors=list(sub.get_all_level_actors())
before=json.loads((out/'current-scene.json').read_text(encoding='utf-8'))['actors']
lookup={a.get_name():a for a in allactors}
preserved=0
for row in before:
    a=lookup.get(row['name'])
    assert a,'An existing actor was removed: '+row['name']
    if a.get_class().get_name()=='RecastNavMesh':continue
    p=a.get_actor_location();assert math.dist([p.x,p.y,p.z],row['position'])<.01,row['name']
    preserved+=1
report={'validation':validation,'navigation':nav,'changes':changes,'added_actors':added,'existing_positions_preserved':preserved,
 'monster_markers':sum(bool(val(a,'LDSceneMember:')) for a in allactors)}
(out/'repair-applied.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(report,ensure_ascii=False))
