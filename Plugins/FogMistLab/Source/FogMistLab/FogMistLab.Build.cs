using UnrealBuildTool;

public class FogMistLab : ModuleRules
{
    public FogMistLab(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "Lockdown" });
        PrivateDependencyModuleNames.AddRange(new[] { "InputCore", "NavigationSystem", "AIModule", "RenderCore", "RHI", "UnrealEd", "EngineSettings" });
    }
}
