import unreal
s=getattr(unreal,'_run_controls_smoke',None)
if s and not s.finished:
    s.finished=True
    unreal.unregister_slate_post_tick_callback(s.handle)
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
