import unreal,json
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
print(json.dumps({'project':unreal.Paths.get_project_file_path(),'pie':level.is_in_play_in_editor()},ensure_ascii=False))
s=getattr(unreal,'_run_controls_smoke',None)
if s and not s.finished:
    s.finished=True
    unreal.unregister_slate_post_tick_callback(s.handle)
if level.is_in_play_in_editor():level.editor_request_end_play()
