import unreal
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
assert w
pc=unreal.GameplayStatics.get_player_controller(w,0)
pc.call_method('ServerEnableDevTest',args=(1,))
roots=unreal.WidgetLibrary.get_all_widgets_of_class(w,unreal.UserWidget,False)
prefix=tuple(r.get_path_name()+'.' for r in roots)
buttons=[b for b in unreal.ObjectIterator(unreal.Button) if b.get_path_name().startswith(prefix) and b.get_name()=='DevTestSkipPreparation']
print('skip buttons',[(b.get_name(),b.get_is_enabled()) for b in buttons])
assert len(buttons)==1
buttons[0].call_method('Execute')
print('Requested existing host DEV action: enter field now')

