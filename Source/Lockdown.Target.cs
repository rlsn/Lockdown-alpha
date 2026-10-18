using UnrealBuildTool;
using System.Collections.Generic;

public class LockdownTarget : TargetRules
{
    public LockdownTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.V7;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
        ExtraModuleNames.Add("Lockdown");
    }
}
