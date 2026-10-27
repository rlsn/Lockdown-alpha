using UnrealBuildTool;

public class LockdownEditor : ModuleRules
{
    public LockdownEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "UnrealEd", "Lockdown", "EnhancedInput", "InputCore", "Json", "NavigationSystem", "UMG", "UMGEditor", "MeshDescription", "StaticMeshDescription", "PhysicsCore" });
    }
}
