using UnrealBuildTool;

public class Lockdown : ModuleRules
{
    public Lockdown(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "UMG", "NavigationSystem", "AIModule", "NetCore" });
        PrivateDependencyModuleNames.AddRange(new[] { "Slate", "SlateCore", "Json", "RHI" });
        ExternalDependencies.Add(System.IO.Path.Combine(ModuleDirectory, "Private", "Development", "LDDemoContentContract.h"));
        ExternalDependencies.Add(System.IO.Path.Combine(ModuleDirectory, "Private", "World", "LDDemoWorldContract.h"));
        RuntimeDependencies.Add("$(ProjectDir)/Content/Lockdown/Data/Demo/demo_content.json", StagedFileType.UFS);
        RuntimeDependencies.Add("$(ProjectDir)/Content/Lockdown/Data/Demo/manifest.json", StagedFileType.UFS);
        RuntimeDependencies.Add("$(ProjectDir)/Content/Lockdown/Data/Demo/world.json", StagedFileType.UFS);
        AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenSSL");
    }
}
