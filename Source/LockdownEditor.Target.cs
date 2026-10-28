using UnrealBuildTool;
using System.Collections.Generic;

public class LockdownEditorTarget : TargetRules
{
    public LockdownEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DisablePlugins.Add("Fab"); // Optional marketplace plugin has unavailable FabToolset rules in this installed engine.
        DefaultBuildSettings = BuildSettingsVersion.V7;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
        ExtraModuleNames.AddRange(new[] { "Lockdown", "LockdownEditor" });
    }
}
