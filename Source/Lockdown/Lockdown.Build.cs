using UnrealBuildTool;

public class Lockdown : ModuleRules
{
    public Lockdown(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "UMG", "NavigationSystem", "AIModule", "NetCore" });
        PrivateDependencyModuleNames.AddRange(new[] { "Slate", "SlateCore", "Json", "RHI" });
        AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenSSL");
        RuntimeDependencies.Add("$(ProjectDir)/Content/Lockdown/Data/economy_rules.json", StagedFileType.UFS);
    }
}
