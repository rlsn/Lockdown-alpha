import unreal,json
from pathlib import Path
out=Path('D:/MonkeyAI/output/run-recovery-2015')
actors=list(unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors())
def val(a,p):return next((str(t)[len(p):] for t in a.tags if str(t).startswith(p)),None)
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
report={'active_monsters':sum(bool(val(a,'LDSceneMember:')) for a in actors),'kept_camps':kept,'paused_camps':paused,'paused_optional_points':disabled_points,'validation':unreal.LDSceneAuthoringLibrary.ground_validation_error(w),'navigation':nav,'saved':True}
(out/'paused-camps.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({k:v for k,v in report.items() if k not in ('paused_camps','kept_camps')},ensure_ascii=False))
