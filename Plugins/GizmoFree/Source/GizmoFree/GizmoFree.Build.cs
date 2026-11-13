using UnrealBuildTool;

public class GizmoFree : ModuleRules
{
    public GizmoFree(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDependencyModuleNames.AddRange(new[] {
            "Core", "CoreUObject", "Engine", "InputCore", "Slate", "SlateCore",
            "UnrealEd", "LevelEditor", "DeveloperSettings"
        });
    }
}
