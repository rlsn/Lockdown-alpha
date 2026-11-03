using UnrealBuildTool;

public class LockdownEditor : ModuleRules
{
    public LockdownEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "UnrealEd", "AssetRegistry", "Lockdown", "EnhancedInput", "InputCore", "Json", "NavigationSystem", "UMG", "UMGEditor", "MeshDescription", "StaticMeshDescription", "PhysicsCore" });
        // Audio: Demo cue import through the engine sound factory.
        PrivateDependencyModuleNames.Add("AudioEditor");
        // Art: CC0 glTF bodies, clips and props imported through Interchange.
        PrivateDependencyModuleNames.AddRange(new[] { "InterchangeCore", "InterchangeEngine", "InterchangePipelines" });
    }
}
