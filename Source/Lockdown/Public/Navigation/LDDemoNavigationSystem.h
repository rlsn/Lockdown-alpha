#pragma once

#include "NavigationSystem.h"
#include "LDDemoNavigationSystem.generated.h"

UCLASS()
class LOCKDOWN_API ULDDemoNavigationSystem : public UNavigationSystemV1
{
    GENERATED_BODY()

public:
    virtual void Configure(const UNavigationSystemConfig& Config) override;
    virtual void InitializeForWorld(UWorld& World, FNavigationSystemRunMode Mode) override;
    bool HasApprovedAgent() const { return bApprovedAgent; }

private:
    bool bApprovedAgent = false;
};
