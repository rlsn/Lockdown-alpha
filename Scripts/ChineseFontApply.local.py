import unreal,json,shutil
from pathlib import Path
out=Path('D:/MonkeyAI/output/chinese-textrender')
folder='/Game/Lockdown/Art/Fonts/ChineseText'
font=unreal.load_asset(folder+'/F_NotoSansSC_Chinese');assert font
material=unreal.EditorAssetLibrary.duplicate_asset('/Engine/EngineMaterials/DefaultTextMaterialOpaque',folder+'/M_ChineseText')
assert material
nodes=[e for e in unreal.ObjectIterator(unreal.MaterialExpressionFontSample) if e.get_path_name().startswith(material.get_path_name()+':') or e.get_path_name().startswith(material.get_path_name()+'.')]
assert nodes,'No font sample in copied Text Render material'
for node in nodes:
    node.set_editor_property('font',font);node.set_editor_property('font_texture_page',0)
material.set_editor_property('two_sided',True)
unreal.MaterialEditingLibrary.recompile_material(material)
assert unreal.EditorAssetLibrary.save_loaded_asset(material)
factory=unreal.BlueprintFactory();factory.set_editor_property('parent_class',unreal.TextRenderActor)
bp=unreal.AssetToolsHelpers.get_asset_tools().create_asset('BP_ChineseText','/Game/Lockdown/Art/Fonts/ChineseText',unreal.Blueprint,factory)
assert bp and unreal.BlueprintEditorLibrary.compile_blueprint(bp)
cls=unreal.EditorAssetLibrary.load_blueprint_class(folder+'/BP_ChineseText')
cdo=unreal.get_default_object(cls);c=cdo.get_component_by_class(unreal.TextRenderComponent)
assert c
c.modify();c.set_font(font);c.set_text_material(material);c.set_text('中文文本');c.set_world_size(64)
assert unreal.EditorAssetLibrary.save_loaded_asset(bp)
sub=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
existing=[]
for a in sub.get_all_level_actors():
    for comp in a.get_components_by_class(unreal.TextRenderComponent):
        existing.append({'actor':a.get_name(),'label':a.get_actor_label(),'component':comp.get_path_name(),'old_font':str(comp.font),'old_material':str(comp.get_material(0)),'text':str(comp.text)})
        comp.modify();comp.set_font(font);comp.set_text_material(material)
        assert comp.font==font and comp.get_material(0)==material
trial=unreal.load_asset(folder+'/F_NotoSansSC_Trial')
if trial:unreal.EditorAssetLibrary.delete_loaded_asset(trial)
report={'font':font.get_path_name(),'material':material.get_path_name(),'blueprint':bp.get_path_name(),'font_nodes':len(nodes),'existing_updated':existing,'map_saved':False}
(out/'applied.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(report,ensure_ascii=False))
print('cdo font',c.font,'material',c.get_material(0),'text',c.text)
