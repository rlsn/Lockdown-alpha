using UnrealBuildTool;

// Lockdown.Rules automation tests and their fixtures. An Editor module: only the editor target builds and loads it, so the
// game target and packaged builds never compile the rule tests. The DEV scenario probes that packaged Development runs
// start from the command line stay in the runtime module (Lockdown/Private/Probes).
public class LockdownTests : ModuleRules
{
    public LockdownTests(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "UMG", "Slate", "SlateCore", "Json", "NavigationSystem", "AIModule", "NetCore", "RHI", "RenderCore", "Lockdown" });
    }
}
