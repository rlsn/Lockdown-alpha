import unreal,json,time
from pathlib import Path
out=Path('D:/MonkeyAI/output/chinese-textrender')
path='/Game/Lockdown/Art/Fonts/ChineseText/F_NotoSansSC_Chinese'
assert not unreal.EditorAssetLibrary.does_asset_exist(path)
factory=unreal.TrueTypeFontFactory()
opts=unreal.FontImportOptionsData()
opts.set_editor_properties({'font_name':'Noto Sans SC','height':20.0,
 'unicode_range':'0020-024F,2000-206F,3000-303F,3400-4DBF,4E00-9FFF,FF00-FFEF',
 'include_ascii_range':True,'texture_page_width':8192,'texture_page_max_height':8192,
 'use_distance_field_alpha':True,'distance_field_scale_factor':2,'enable_antialiasing':True})
factory.get_editor_property('import_options').set_editor_property('data',opts)
task=unreal.AssetImportTask();task.set_editor_property('automated',True)
factory.set_editor_property('asset_import_task',task);factory.set_editor_property('edit_after_new',False)
started=time.monotonic()
font=unreal.AssetToolsHelpers.get_asset_tools().create_asset('F_NotoSansSC_Chinese','/Game/Lockdown/Art/Fonts/ChineseText',unreal.Font,factory)
assert font
chars=font.get_editor_property('characters')
pages=sorted(set(c.get_editor_property('texture_index') for c in chars))
assert font.get_editor_property('font_cache_type')==unreal.FontCacheType.OFFLINE
assert len(chars)>27000 and pages==[0],(len(chars),pages)
assert unreal.EditorAssetLibrary.save_loaded_asset(font)
report={'font':font.get_path_name(),'characters':len(chars),'pages':pages,'elapsed_seconds':time.monotonic()-started,
 'font_name':font.get_editor_property('import_options').get_editor_property('font_name'),'range':opts.get_editor_property('unicode_range')}
(out/'font-created.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(report,ensure_ascii=False))
