#include "Navigation/LDDemoNavigationSystem.h"
#include "Core/LDLog.h"

#include "Development/LDDemoContentCatalog.h"
#include "Framework/LDDemoEntry.h"
#include "Region/LDMapRegistry.h"

void ULDDemoNavigationSystem::Configure(const UNavigationSystemConfig& Config)
{
    bApprovedAgent = false;
    TSharedPtr<const FLDDemoContentSnapshot> Content;
    FCollisionShape Capsule;
    FString Error;
    if (!LDDemoEntry::LoadRuntimeContent(Content, Error) || !ULDMapRegistry::ContentCapsule(*Content, Capsule, Error))
    {
        SupportedAgents.Reset();
        bAutoCreateNavigationData = false;
        UE_LOG(LogLDNav, Error, TEXT("LD_DEMO_NAVIGATION_CONFIG_REJECTED %s"), *Error);
        return;
    }
    FNavDataConfig Agent = GetDefault<UNavigationSystemV1>()->GetDefaultSupportedAgentConfig();
    Agent.Name = TEXT("DemoBody");
    Agent.AgentRadius = Capsule.GetCapsuleRadius();
    Agent.AgentHeight = Capsule.GetCapsuleHalfHeight() * 2;
    Agent.bCanJump = false;
    // Base configuration reapplies CDO agents. Replace the instance list before registration.
    Super::Configure(Config);
    SupportedAgents = {Agent};
    DefaultAgentName = Agent.Name;
    bAllowClientSideNavigation = true;
    bAutoCreateNavigationData = true;
    bApprovedAgent = true;
}

void ULDDemoNavigationSystem::InitializeForWorld(UWorld& World, FNavigationSystemRunMode Mode)
{
    if (!bApprovedAgent)
    {
        UE_LOG(LogLDNav, Error, TEXT("LD_DEMO_NAVIGATION_INIT_REJECTED No approved native agent configuration."));
        return;
    }
    Super::InitializeForWorld(World, Mode);
}
