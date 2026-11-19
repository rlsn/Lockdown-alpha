import unreal,json
from pathlib import Path
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
assert w
pc=unreal.GameplayStatics.get_player_controller(w,0)
pawn=pc.get_controlled_pawn()
assert pawn and pc.get_view_target()==pawn
actors=unreal.GameplayStatics.get_all_actors_of_class(w,unreal.LDLayoutPoint)
paused=[{'actor':a.get_name(),'label':a.get_actor_label(),'tags':[str(t) for t in a.tags if str(t).startswith('LDRunMapPaused:')]} for a in actors if any(str(t).startswith('LDRunMapPaused:') for t in a.tags)]
roots=unreal.WidgetLibrary.get_all_widgets_of_class(w,unreal.UserWidget,False)
prefix=tuple(r.get_path_name()+'.' for r in roots)
for b in unreal.ObjectIterator(unreal.Button):
    if b.get_name()=='DevTestPeaceOn' and b.get_path_name().startswith(prefix):b.call_method('Execute');break
report={'in_field':True,'pawn':pawn.get_path_name(),'camera':pc.get_view_target().get_path_name(),'move_input_ignored':pc.is_move_input_ignored(),'movement_mode':str(pawn.get_component_by_class(unreal.CharacterMovementComponent).get_editor_property('movement_mode')),'paused_markers':paused,'pie_left_running':True}
Path('D:/MonkeyAI/output/run-recovery-2015/ready.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({k:v for k,v in report.items() if k!='paused_markers'},ensure_ascii=False))
