import unreal,shutil,json,time
from pathlib import Path
out=Path('D:/MonkeyAI/output/slope50-no-ledge/pre-restart');out.mkdir(exist_ok=True)
assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
maps=list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages());assets=list(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())
copies=[]
for package in maps+assets:
    name=package.get_path_name()
    if not name.startswith('/Game/'):continue
    relative=name.removeprefix('/Game/')+('.umap' if package in maps else '.uasset')
    src=Path('D:/Lockdown-alpha/Content')/relative
    dest=out/'before-save'/relative;dest.parent.mkdir(parents=True,exist_ok=True)
    if src.exists():shutil.copy2(src,dest)
    copies.append((str(src),relative))
assert unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True,True)
for filename,relative in copies:
    dest=out/'saved-current'/relative;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(filename,dest)
(out/'saved.json').write_text(json.dumps(copies,ensure_ascii=False,indent=2),encoding='utf-8')
def quit_when_ready(delta):
    if time.monotonic()<due:return
    unreal.unregister_slate_post_tick_callback(handle)
    assert not unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()
    assert not unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()
    unreal.SystemLibrary.quit_editor()
due=time.monotonic()+2
handle=unreal.register_slate_post_tick_callback(quit_when_ready)
print('Backed up and saved current level edits; queued restart for slope/ledge settings.')
