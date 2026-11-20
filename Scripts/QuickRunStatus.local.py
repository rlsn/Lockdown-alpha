import json,unreal
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
print('game',w)
smoke=getattr(unreal,'_city_flow_smoke',None)
print('smoke',{'finished':smoke.finished,'actions':smoke.action_index,'elapsed':__import__('time').monotonic()-smoke.started} if smoke else None)
if w:
    roots=unreal.WidgetLibrary.get_all_widgets_of_class(w,unreal.UserWidget,False)
    prefixes=tuple(r.get_path_name()+'.' for r in roots)
    texts=[str(t.get_text()) for t in unreal.ObjectIterator(unreal.TextBlock) if t.get_path_name().startswith(prefixes)]
    print('phase_texts',[t for t in texts if any(k in t for k in ('野外','休息','阻止','失败','入场','返回'))])
    pc=unreal.GameplayStatics.get_player_controller(w,0)
    print('pawn',pc.get_controlled_pawn(),'camera',pc.get_view_target())
    print('monsters',len(unreal.GameplayStatics.get_all_actors_of_class(w,unreal.LDCampMemberCharacter)))
