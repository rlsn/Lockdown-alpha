import unreal,json
from pathlib import Path
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
assert w
actors=unreal.GameplayStatics.get_all_actors_of_class(w,unreal.LDLayoutPoint)
def val(a,p):return next((str(t)[len(p):] for t in a.tags if str(t).startswith(p)),None)
cap=next(a for a in actors if val(a,'LDSceneAnchor:')=='capture')
goal=unreal.NavigationSystemV1.project_point_to_navigation(w,cap.get_actor_location(),None,None,unreal.Vector(100,100,100))
rows=[]
for a in actors:
    if not val(a,'LDSceneMember:'):continue
    p=a.get_actor_location()
    start=unreal.NavigationSystemV1.project_point_to_navigation(w,p,None,None,unreal.Vector(60,60,150))
    row={'id':val(a,'LDSceneMember:'),'actor':a.get_name(),'position':[p.x,p.y,p.z]}
    for label,x,y in [('to_centre',start,goal),('from_centre',goal,start)]:
        path=unreal.NavigationSystemV1.find_path_to_location_synchronously(w,x,y) if x is not None and y is not None else None
        row[label]=bool(path and path.is_valid() and not path.is_partial())
    rows.append(row)
pc=unreal.GameplayStatics.get_player_controller(w,0)
report={'count':len(rows),'both_connected':sum(r['to_centre'] and r['from_centre'] for r in rows),'failures':[r for r in rows if not r['to_centre'] or not r['from_centre']],
'pawn':str(pc.get_controlled_pawn()),'camera':str(pc.get_view_target()),'monsters_alive':len(unreal.GameplayStatics.get_all_actors_of_class(w,unreal.LDCampMemberCharacter))}
Path('D:/MonkeyAI/output/quick-run-repair/routes.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(report,ensure_ascii=False))
