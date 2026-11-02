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
        RuntimeDependencies.Add("$(ProjectDir)/Content/Lockdown/Data/V08/combat_b84.json", StagedFileType.UFS);
        RuntimeDependencies.Add("$(ProjectDir)/Content/Lockdown/Data/V08/equipment_b84.json", StagedFileType.UFS);
        RuntimeDependencies.Add("$(ProjectDir)/Content/Lockdown/Data/V08/acquisition_b84.json", StagedFileType.UFS);
        AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenSSL");
        RuntimeDependencies.Add("$(ProjectDir)/Content/Lockdown/Data/V09/map_b90.json", StagedFileType.UFS);
        RuntimeDependencies.Add("$(ProjectDir)/Content/Lockdown/Data/V09/environment_b91.json", StagedFileType.UFS);
        RuntimeDependencies.Add("$(ProjectDir)/Content/Lockdown/Data/V09/objects_b91.json", StagedFileType.UFS);
        RuntimeDependencies.Add("$(ProjectDir)/Content/Lockdown/Data/V09/tools_b91.json", StagedFileType.UFS);
        RuntimeDependencies.Add("$(ProjectDir)/Content/Lockdown/Data/V09/traversal_b91.json", StagedFileType.UFS);
        RuntimeDependencies.Add("$(ProjectDir)/Content/Lockdown/Data/economy_rules.json", StagedFileType.UFS);
        RuntimeDependencies.Add("$(ProjectDir)/Content/Lockdown/Data/V07/combat.json", StagedFileType.UFS);
        RuntimeDependencies.Add("$(ProjectDir)/Content/Lockdown/Data/V07/equipment.json", StagedFileType.UFS);
        RuntimeDependencies.Add("$(ProjectDir)/Content/Lockdown/Data/V07/world.json", StagedFileType.UFS);
    }
}
