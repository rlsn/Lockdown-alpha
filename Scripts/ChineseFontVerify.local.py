import unreal,json,shutil
from pathlib import Path
out=Path('D:/MonkeyAI/output/chinese-textrender')
folder='/Game/Lockdown/Art/Fonts/ChineseText'
font=unreal.load_asset(folder+'/F_NotoSansSC_Chinese')
mat=unreal.load_asset(folder+'/M_ChineseText')
cls=unreal.EditorAssetLibrary.load_blueprint_class(folder+'/BP_ChineseText')
sub=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
preview=sub.spawn_actor_from_class(cls,unreal.Vector(0,0,100000),unreal.Rotator())
assert preview
c=preview.get_component_by_class(unreal.TextRenderComponent)
assert c.font==font and c.get_material(0)==mat and str(c.text)=='中文文本'
size=c.get_text_local_size()
assert size.y>0 and size.z>0,str(size)
assert sub.destroy_actor(preview)
textures=[t for t in unreal.ObjectIterator(unreal.Texture2D) if t.get_path_name().startswith(font.get_path_name()+':') or t.get_path_name().startswith(font.get_path_name()+'.')]
print('textures',[t.get_path_name() for t in textures])
assert len(textures)==1
task=unreal.AssetExportTask();task.object=textures[0];task.filename=str(out/'atlas.png');task.automated=True;task.prompt=False;task.replace_identical=True;task.exporter=unreal.TextureExporterPNG()
assert unreal.Exporter.run_asset_export_task(task)
chars=font.get_editor_property('characters')
regions=[]
for ch in '中文测试青岛栈桥百盛七中怪物营地':
    code=ord(ch)
    index=7360+code-0x4e00
    data={k:chars[index].get_editor_property(k) for k in ('start_u','start_v','u_size','v_size','texture_index','vertical_offset')}
    assert data['u_size']>0 and data['v_size']>0,(ch,index,data)
    regions.append({'char':ch,'index':index,**data})
mapfile=Path('D:/Lockdown-alpha/Content/Lockdown/Generated/Maps/L_Demo.umap')
shutil.copy2(mapfile,out/'before-font-save.umap')
assert unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
unreal.EditorAssetLibrary.sync_browser_to_objects([folder+'/BP_ChineseText'])
report={'preset_verified':True,'text_bounds':str(size),'atlas':str(out/'atlas.png'),'glyphs':regions,'map_saved':True}
(out/'verified.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(report,ensure_ascii=False))

