import unreal
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
h=unreal.SystemLibrary.line_trace_single_for_objects(w,unreal.Vector(-1267,-4347,20000),unreal.Vector(-1267,-4347,-10000),[unreal.ObjectTypeQuery.OBJECT_TYPE_QUERY1],False,[],unreal.DrawDebugTrace.NONE)
print('hit',h,'dict',h.to_dict(),'tuple',h.to_tuple())
print('editor impact',h.get_editor_property('impact_point'),h.get_editor_property('impact_normal'))
