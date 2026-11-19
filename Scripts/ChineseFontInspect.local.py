import unreal,json
from pathlib import Path
print('project',unreal.Paths.get_project_file_path())
print('pie',unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor())
for name in ('TrueTypeFontFactory','FontFactory','FontImportOptions','FontImportOptionsData','Font','MaterialExpressionFontSample','MaterialExpressionFontSampleParameter'):
    cls=getattr(unreal,name,None)
    print(name,cls.__doc__ if cls else None)
sub=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
rows=[]
for a in sub.get_all_level_actors():
    for c in a.get_components_by_class(unreal.TextRenderComponent):
        rows.append({'actor':a.get_name(),'label':a.get_actor_label(),'component':c.get_path_name(),'text':str(c.text),'font':str(c.font),'material':str(c.get_material(0)),'selected':a in sub.get_selected_level_actors()})
Path('D:/MonkeyAI/output/chinese-textrender/text-before.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2),encoding='utf-8')
print('text_components',json.dumps(rows,ensure_ascii=False))
