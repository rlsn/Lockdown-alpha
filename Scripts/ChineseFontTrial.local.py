import unreal,json
from pathlib import Path
factory=unreal.TrueTypeFontFactory()
print('factory options',factory.get_editor_property('import_options'))
opts=unreal.FontImportOptionsData()
opts.set_editor_properties({'font_name':'Noto Sans SC','height':20.0,'unicode_range':'0020-007E,3000-303F,4E2D-4E2D,6587-6587,6D4B-6D4B,8BD5-8BD5','include_ascii_range':True,'texture_page_width':1024,'texture_page_max_height':1024,'use_distance_field_alpha':True,'distance_field_scale_factor':4,'enable_antialiasing':True})
factory.get_editor_property('import_options').set_editor_property('data',opts)
task=unreal.AssetImportTask();task.set_editor_property('automated',True);factory.set_editor_property('asset_import_task',task)
factory.set_editor_property('edit_after_new',False)
font=unreal.AssetToolsHelpers.get_asset_tools().create_asset('F_NotoSansSC_Trial','/Game/Lockdown/Art/Fonts/ChineseText',unreal.Font,factory)
assert font
print('FONT',font,'cache',font.font_cache_type,'chars',len(font.characters))
for p in ('textures','is_remapped','char_remap','import_options'):
    try:print(p,font.get_editor_property(p))
    except Exception as e:print('not exposed',p,str(e))
print('FontCharacter',unreal.FontCharacter.__doc__)
print('BlueprintFactory',unreal.BlueprintFactory.__doc__)
print('compile blueprint',unreal.BlueprintEditorLibrary.compile_blueprint.__doc__)


