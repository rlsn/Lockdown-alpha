using UnrealBuildTool;
public class ParallelPointerUE : ModuleRules
{
    public ParallelPointerUE(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "UnrealEd", "Slate", "SlateCore", "ApplicationCore", "InputCore", "Json" });
        PublicSystemLibraries.AddRange(new[] { "user32.lib", "advapi32.lib" });
    }
}
